//===- GraphicsPipeline.cpp - VkPipeline graphics state ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GraphicsPipeline.h"
#include "Descriptor.h"
#include "Diagnostics.h"
#include "Format.h"
#include "Icd.h"
#include "Objects.h"
#include "PhysicalDeviceInfo.h"
#include "Pipeline.h"
#include "PipelineCache.h"
#include "RenderPass.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
#include "feme/Graphics/Patch.h"
#include "feme/Graphics/Tessellation.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

#include <optional>

using namespace feme::vulkan;
using namespace llvm;

namespace {

using feme::graphics::AttachmentFormat;
using feme::graphics::BlendFactor;
using feme::graphics::BlendOp;
using feme::graphics::BlendState;
using feme::graphics::CompareOp;
using feme::graphics::CullMode;
using feme::graphics::FrontFace;
using feme::graphics::LineRasterizationMode;
using feme::graphics::LogicOp;
using feme::graphics::PrimitiveTopology;
using feme::graphics::StencilFaceState;
using feme::graphics::StencilOp;

//===----------------------------------------------------------------------===//
// Fixed-function state translation
//===----------------------------------------------------------------------===//

std::optional<PrimitiveTopology> mapTopology(VkPrimitiveTopology Topology) {
  switch (Topology) {
  case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    return PrimitiveTopology::PointList;
  case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    return PrimitiveTopology::LineList;
  case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
    return PrimitiveTopology::LineStrip;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    return PrimitiveTopology::TriangleList;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    return PrimitiveTopology::TriangleStrip;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
    return PrimitiveTopology::TriangleFan;
  case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
    // (roadmap H4b) Only legal on a pipeline declaring tessellation
    // stages, and vice versa -- see `translateFixedFunctionState`'s own
    // check, right after this is called.
    return PrimitiveTopology::PatchList;
  default:
    // The four adjacency topologies (list/strip, line/triangle) need a
    // geometry stage (V7): "validated against the advertised topologies".
    return std::nullopt;
  }
}

std::optional<CullMode> mapCullMode(VkCullModeFlags Cull) {
  switch (Cull) {
  case VK_CULL_MODE_NONE:
    return CullMode::None;
  case VK_CULL_MODE_FRONT_BIT:
    return CullMode::Front;
  case VK_CULL_MODE_BACK_BIT:
    return CullMode::Back;
  case VK_CULL_MODE_FRONT_AND_BACK:
    return CullMode::FrontAndBack;
  default:
    return std::nullopt;
  }
}

// (roadmap F5) `VkLineRasterizationModeKHR` -> `feme::graphics::
// LineRasterizationMode`. `VK_LINE_RASTERIZATION_MODE_DEFAULT_KHR` means
// "whatever this implementation's own default line style is" -- since
// this driver's default (and only style when no `VkPipelineRasterization
// LineStateCreateInfo` is chained at all) is the same `Rectangular`
// style `RECTANGULAR_KHR` names explicitly, both map to the same value.
std::optional<LineRasterizationMode>
mapLineRasterizationMode(VkLineRasterizationModeKHR Mode) {
  switch (Mode) {
  case VK_LINE_RASTERIZATION_MODE_DEFAULT_KHR:
  case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_KHR:
    return LineRasterizationMode::Rectangular;
  case VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR:
    return LineRasterizationMode::Bresenham;
  case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_KHR:
    return LineRasterizationMode::RectangularSmooth;
  default:
    return std::nullopt;
  }
}

std::optional<CompareOp> mapCompareOp(VkCompareOp Op) {
  switch (Op) {
  case VK_COMPARE_OP_NEVER:
    return CompareOp::Never;
  case VK_COMPARE_OP_LESS:
    return CompareOp::Less;
  case VK_COMPARE_OP_EQUAL:
    return CompareOp::Equal;
  case VK_COMPARE_OP_LESS_OR_EQUAL:
    return CompareOp::LessEqual;
  case VK_COMPARE_OP_GREATER:
    return CompareOp::Greater;
  case VK_COMPARE_OP_NOT_EQUAL:
    return CompareOp::NotEqual;
  case VK_COMPARE_OP_GREATER_OR_EQUAL:
    return CompareOp::GreaterEqual;
  case VK_COMPARE_OP_ALWAYS:
    return CompareOp::Always;
  default:
    return std::nullopt;
  }
}

std::optional<StencilOp> mapStencilOp(VkStencilOp Op) {
  switch (Op) {
  case VK_STENCIL_OP_KEEP:
    return StencilOp::Keep;
  case VK_STENCIL_OP_ZERO:
    return StencilOp::Zero;
  case VK_STENCIL_OP_REPLACE:
    return StencilOp::Replace;
  case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
    return StencilOp::IncrementClamp;
  case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
    return StencilOp::DecrementClamp;
  case VK_STENCIL_OP_INVERT:
    return StencilOp::Invert;
  case VK_STENCIL_OP_INCREMENT_AND_WRAP:
    return StencilOp::IncrementWrap;
  case VK_STENCIL_OP_DECREMENT_AND_WRAP:
    return StencilOp::DecrementWrap;
  default:
    return std::nullopt;
  }
}

std::optional<BlendFactor> mapBlendFactor(VkBlendFactor Factor) {
  switch (Factor) {
  case VK_BLEND_FACTOR_ZERO:
    return BlendFactor::Zero;
  case VK_BLEND_FACTOR_ONE:
    return BlendFactor::One;
  case VK_BLEND_FACTOR_SRC_COLOR:
    return BlendFactor::SrcColor;
  case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
    return BlendFactor::OneMinusSrcColor;
  case VK_BLEND_FACTOR_DST_COLOR:
    return BlendFactor::DstColor;
  case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
    return BlendFactor::OneMinusDstColor;
  case VK_BLEND_FACTOR_SRC_ALPHA:
    return BlendFactor::SrcAlpha;
  case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
    return BlendFactor::OneMinusSrcAlpha;
  case VK_BLEND_FACTOR_DST_ALPHA:
    return BlendFactor::DstAlpha;
  case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
    return BlendFactor::OneMinusDstAlpha;
  case VK_BLEND_FACTOR_CONSTANT_COLOR:
    return BlendFactor::ConstantColor;
  case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
    return BlendFactor::OneMinusConstantColor;
  case VK_BLEND_FACTOR_CONSTANT_ALPHA:
    return BlendFactor::ConstantAlpha;
  case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
    return BlendFactor::OneMinusConstantAlpha;
  case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
    return BlendFactor::SrcAlphaSaturate;
  case VK_BLEND_FACTOR_SRC1_COLOR:
    return BlendFactor::Src1Color;
  case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
    return BlendFactor::OneMinusSrc1Color;
  case VK_BLEND_FACTOR_SRC1_ALPHA:
    return BlendFactor::Src1Alpha;
  case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
    return BlendFactor::OneMinusSrc1Alpha;
  default:
    return std::nullopt;
  }
}

std::optional<BlendOp> mapBlendOp(VkBlendOp Op) {
  switch (Op) {
  case VK_BLEND_OP_ADD:
    return BlendOp::Add;
  case VK_BLEND_OP_SUBTRACT:
    return BlendOp::Subtract;
  case VK_BLEND_OP_REVERSE_SUBTRACT:
    return BlendOp::ReverseSubtract;
  case VK_BLEND_OP_MIN:
    return BlendOp::Min;
  case VK_BLEND_OP_MAX:
    return BlendOp::Max;
  default:
    return std::nullopt;
  }
}

std::optional<LogicOp> mapLogicOp(VkLogicOp Op) {
  switch (Op) {
  case VK_LOGIC_OP_CLEAR:
    return LogicOp::Clear;
  case VK_LOGIC_OP_AND:
    return LogicOp::And;
  case VK_LOGIC_OP_AND_REVERSE:
    return LogicOp::AndReverse;
  case VK_LOGIC_OP_COPY:
    return LogicOp::Copy;
  case VK_LOGIC_OP_AND_INVERTED:
    return LogicOp::AndInverted;
  case VK_LOGIC_OP_NO_OP:
    return LogicOp::NoOp;
  case VK_LOGIC_OP_XOR:
    return LogicOp::Xor;
  case VK_LOGIC_OP_OR:
    return LogicOp::Or;
  case VK_LOGIC_OP_NOR:
    return LogicOp::Nor;
  case VK_LOGIC_OP_EQUIVALENT:
    return LogicOp::Equivalent;
  case VK_LOGIC_OP_INVERT:
    return LogicOp::Invert;
  case VK_LOGIC_OP_OR_REVERSE:
    return LogicOp::OrReverse;
  case VK_LOGIC_OP_COPY_INVERTED:
    return LogicOp::CopyInverted;
  case VK_LOGIC_OP_OR_INVERTED:
    return LogicOp::OrInverted;
  case VK_LOGIC_OP_NAND:
    return LogicOp::Nand;
  case VK_LOGIC_OP_SET:
    return LogicOp::Set;
  default:
    return std::nullopt;
  }
}

std::optional<DynamicStateBits> mapDynamicState(VkDynamicState State) {
  switch (State) {
  case VK_DYNAMIC_STATE_VIEWPORT:
  // (roadmap C4c) `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT` is the same
  // effective dynamic state as `VIEWPORT` (the Vulkan spec forbids a
  // pipeline from declaring both): `resolveViewport` already reads
  // `DynamicGraphicsState::Viewports` whenever `DynamicStateViewport` is
  // set, and `vkCmdSetViewportWithCount{,EXT}` writes into that same
  // array state.
  case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
    return DynamicStateViewport;
  case VK_DYNAMIC_STATE_SCISSOR:
  case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
    return DynamicStateScissor;
  case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
    return DynamicStateBlendConstants;
  case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
    return DynamicStateStencilReference;
  case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
    return DynamicStateStencilCompareMask;
  case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
    return DynamicStateStencilWriteMask;
  case VK_DYNAMIC_STATE_CULL_MODE:
    return DynamicStateCullMode;
  case VK_DYNAMIC_STATE_FRONT_FACE:
    return DynamicStateFrontFace;
  case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
    return DynamicStateDepthTestEnable;
  case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
    return DynamicStateDepthWriteEnable;
  case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
    return DynamicStateDepthCompareOp;
  case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
    return DynamicStateDepthBoundsTestEnable;
  case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
    return DynamicStateStencilTestEnable;
  case VK_DYNAMIC_STATE_STENCIL_OP:
    return DynamicStateStencilOp;
  case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
    return DynamicStatePrimitiveTopology;
  case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
    return DynamicStateVertexInputBindingStride;
  case VK_DYNAMIC_STATE_LINE_WIDTH:
    return DynamicStateLineWidth;
  case VK_DYNAMIC_STATE_LINE_STIPPLE_KHR:
    return DynamicStateLineStipple;
  default:
    return std::nullopt;
  }
}

/// Whether \p Format may be fetched as a vertex attribute: the subset
/// `feme::graphics`' own `decodeAttribute` (Executor.cpp) implements.
bool isSupportedVertexAttributeFormat(feme::cpu::ResourceFormat Format) {
  switch (Format) {
  case feme::cpu::ResourceFormat::R32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32B32_FLOAT:
  case feme::cpu::ResourceFormat::R32G32B32A32_FLOAT:
  case feme::cpu::ResourceFormat::R32_UINT:
  case feme::cpu::ResourceFormat::R32G32_UINT:
  case feme::cpu::ResourceFormat::R32G32B32_UINT:
  case feme::cpu::ResourceFormat::R32G32B32A32_UINT:
  case feme::cpu::ResourceFormat::R32_SINT:
  case feme::cpu::ResourceFormat::R32G32_SINT:
  case feme::cpu::ResourceFormat::R32G32B32_SINT:
  case feme::cpu::ResourceFormat::R32G32B32A32_SINT:
  case feme::cpu::ResourceFormat::R8G8B8A8_UNORM:
  case feme::cpu::ResourceFormat::R8G8B8A8_UNORM_SRGB:
  case feme::cpu::ResourceFormat::R8G8B8A8_SNORM:
  case feme::cpu::ResourceFormat::R8G8B8A8_UINT:
  case feme::cpu::ResourceFormat::R8G8B8A8_SINT:
    return true;
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Stage compilation
//===----------------------------------------------------------------------===//

/// Compiles one `VkPipelineShaderStageCreateInfo` into a
/// `feme::cpu::CompiledStage` for \p Stage: the same import/translate flow
/// the compute path uses, plus `feme::graphics::CanonicalizeStagePass` --
/// which rewrites the SPIR-V interface accesses into the `feme.stage.*`
/// family and builds the entry's `feme::EntrySignature` -- and
/// `StageCompileOptions` naming the stage (see "Graphics pipeline state").
///
/// \p EntryPointOverride selects a different entry point name than
/// \p StageInfo's own (falling back to `StageInfo.pName`, `"main"` by
/// default, when empty). This is only needed for a tessellation-control
/// module (roadmap H4b): both of its two split phases (roadmap H4a's
/// control-point function, kept under the module's own entry point name,
/// and its `<entry>.patchconstant` sibling) are tagged
/// `feme::ShaderStage::Hull` alike, so `feme::cpu::CompiledStage::create`'s
/// own "exactly one entry point of this stage" auto-detection cannot tell
/// them apart -- each of the two calls compiling one of them must instead
/// name its own entry point explicitly.
///
/// \p OutState, if non-null, is filled in from the selected entry point's
/// own `feme.tessellation.*` attributes (`feme::graphics::
/// getTessellationState`) before the module is hidden inside
/// `feme::cpu::CompiledStage::create` -- the last point at which the
/// un-JIT-ed `llvm::Function` is available to read them from.
Expected<std::shared_ptr<feme::cpu::CompiledStage>> compileGraphicsStage(
    feme::Context &Ctx, const VkPipelineShaderStageCreateInfo &StageInfo,
    feme::ShaderStage Stage, llvm::StringRef EntryPointOverride = {},
    std::optional<feme::graphics::TessellationState> *OutState = nullptr) {
  if (!StageInfo.module)
    return createStringError(inconvertibleErrorCode(),
                             "graphics pipeline stage has no VkShaderModule");
  // Specialization constants are not resolved for a graphics stage yet: the
  // compute path's own resolution is group-size-specific (GroupSize.h), and
  // nothing here consumes a specialized value. Accepting the structure
  // silently would compile the shader's default constants instead of the
  // application's, so it is rejected.
  if (StageInfo.pSpecializationInfo &&
      StageInfo.pSpecializationInfo->mapEntryCount != 0)
    return createStringError(inconvertibleErrorCode(),
                             "specialization constants are not implemented "
                             "for a graphics stage yet");

  auto *Module = fromHandle<ShaderModule>(StageInfo.module);
  std::string DefaultEntryPoint = StageInfo.pName ? StageInfo.pName : "main";
  llvm::StringRef EntryPoint =
      EntryPointOverride.empty() ? llvm::StringRef(DefaultEntryPoint)
                                 : EntryPointOverride;

  Expected<feme::Module> AsLLVMIR = importShaderModule(Ctx, Module->words());
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  ModuleAnalysisManager MAM;
  feme::graphics::CanonicalizeStagePass().run(AsLLVMIR->getLLVMModule(), MAM);

  if (OutState) {
    llvm::Function *Entry = AsLLVMIR->getLLVMModule().getFunction(EntryPoint);
    *OutState = Entry ? feme::graphics::getTessellationState(*Entry)
                      : std::nullopt;
  }

  feme::cpu::StageCompileOptions Opts;
  Opts.Stage = Stage;
  Opts.EntryPoint = EntryPoint;
  Expected<std::unique_ptr<feme::cpu::CompiledStage>> Compiled =
      feme::cpu::CompiledStage::create(Ctx, std::move(*AsLLVMIR), Opts);
  if (!Compiled)
    return Compiled.takeError();
  return std::shared_ptr<feme::cpu::CompiledStage>(std::move(*Compiled));
}

const feme::SignatureElement *
findSystemValue(const feme::EntrySignature &Sig, feme::SignatureDirection Dir,
                feme::SignatureSystemValue SysVal) {
  for (const feme::SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Dir && Elt.SystemValue == SysVal)
      return &Elt;
  return nullptr;
}

const feme::SignatureElement *findLocation(const feme::EntrySignature &Sig,
                                           feme::SignatureDirection Dir,
                                           uint32_t Location) {
  for (const feme::SignatureElement &Elt : Sig.Elements)
    if (Elt.Direction == Dir &&
        Elt.SystemValue == feme::SignatureSystemValue::None && Elt.Location &&
        *Elt.Location == Location)
      return &Elt;
  return nullptr;
}

Expected<feme::EntrySignature>
getStageSignature(const feme::cpu::CompiledStage &Stage) {
  std::vector<uint8_t> Bytes = Stage.getArtifactInfo().Signature;
  if (Bytes.empty())
    return createStringError(inconvertibleErrorCode(),
                             "compiled stage carries no signature "
                             "reflection");
  return feme::parseSignature(Bytes);
}

/// Validates the vertex -> fragment interface against the core reflection
/// G0 produces: "Cross-stage interface matching is validated at pipeline
/// creation ... and a mismatch is a pipeline-creation failure with a
/// diagnostic, never a silently mislinked varying." Also checks the two
/// interface obligations the executor itself has -- an `SV_Position`
/// output from whichever stage the rasterizer actually reads it from, and
/// one `SV_TargetN` fragment output per color attachment -- here, at
/// creation, rather than leaving them for the first draw.
///
/// \p FragmentStage is `nullptr` for a pipeline that legally omitted its
/// fragment stage (roadmap H2j, only possible when \p ColorAttachmentCount
/// is 0, matching `VUID-VkGraphicsPipelineCreateInfo-pStages-06894`'s own
/// condition): every fragment-side check below (varying linkage, per-
/// attachment outputs) is skipped in that case, since there is no fragment
/// signature to check them against.
///
/// \p DomainStage is non-`nullptr` for a pipeline with tessellation stages
/// (roadmap H4b). When present, it -- not \p VertexStage -- is the stage
/// whose own output is rasterized (`PatchPipeline.cpp`'s
/// `runPatchPipeline`), so the `SV_Position` requirement below is checked
/// against it instead: a tessellation-evaluation shader computes its own
/// clip-space position from `gl_TessCoord`/patch data, and per
/// `dEQP-VK.tessellation.winding.*`'s own real-world shape, the vertex
/// stage feeding it may legally write nothing at all (an empty `void
/// main(void) {}`) when the evaluation shader never reads a per-vertex
/// input back via `gl_in[]` (roadmap H4h).
Error validateStageInterfaces(const feme::cpu::CompiledStage &VertexStage,
                              const feme::cpu::CompiledStage *FragmentStage,
                              const feme::cpu::CompiledStage *DomainStage,
                              uint32_t ColorAttachmentCount,
                              llvm::ArrayRef<VertexInputAttribute> Attributes) {
  Expected<feme::EntrySignature> VSSig = getStageSignature(VertexStage);
  if (!VSSig)
    return VSSig.takeError();

  std::optional<feme::EntrySignature> DomainSig;
  if (DomainStage) {
    Expected<feme::EntrySignature> Parsed = getStageSignature(*DomainStage);
    if (!Parsed)
      return Parsed.takeError();
    DomainSig = std::move(*Parsed);
  }
  const feme::EntrySignature &PositionSig = DomainSig ? *DomainSig : *VSSig;

  const feme::SignatureElement *Position =
      findSystemValue(PositionSig, feme::SignatureDirection::Output,
                      feme::SignatureSystemValue::Position);
  if (!Position || Position->ComponentCount != 4)
    return createStringError(
        inconvertibleErrorCode(), "%s stage does not write a 4-component "
                                  "SV_Position output",
        DomainStage ? "tessellation evaluation" : "vertex");

  if (FragmentStage) {
    Expected<feme::EntrySignature> FSSig = getStageSignature(*FragmentStage);
    if (!FSSig)
      return FSSig.takeError();

    for (const feme::SignatureElement &FSIn : FSSig->Elements) {
      if (FSIn.Direction != feme::SignatureDirection::Input ||
          FSIn.SystemValue != feme::SignatureSystemValue::None)
        continue;
      if (!FSIn.Location)
        return createStringError(inconvertibleErrorCode(),
                                 "fragment input element %u has no location "
                                 "to link against a vertex output",
                                 FSIn.ElementID);
      const feme::SignatureElement *VSOut = findLocation(
          *VSSig, feme::SignatureDirection::Output, *FSIn.Location);
      if (!VSOut)
        return createStringError(inconvertibleErrorCode(),
                                 "fragment input location %u has no matching "
                                 "vertex stage output",
                                 *FSIn.Location);
      if (VSOut->ComponentCount != FSIn.ComponentCount ||
          VSOut->ComponentType != FSIn.ComponentType)
        return createStringError(inconvertibleErrorCode(),
                                 "vertex output and fragment input at "
                                 "location %u disagree on component "
                                 "count/type",
                                 *FSIn.Location);
    }

    for (uint32_t I = 0; I != ColorAttachmentCount; ++I) {
      const feme::SignatureElement *Color =
          findLocation(*FSSig, feme::SignatureDirection::Output, I);
      if (!Color || Color->ComponentCount != 4 ||
          Color->ComponentType != feme::SignatureComponentType::Float)
        return createStringError(inconvertibleErrorCode(),
                                 "fragment stage has no 4-component "
                                 "floating-point output at location %u "
                                 "(SV_Target%u)",
                                 I, I);
    }
  }
  assert((FragmentStage || ColorAttachmentCount == 0) &&
         "a fragment-less pipeline must not declare color attachments");

  // Every located vertex *input* must be supplied by a vertex attribute:
  // an unbound one would read as zero at every vertex, which is a silently
  // wrong image rather than a diagnosable failure.
  for (const feme::SignatureElement &VSIn : VSSig->Elements) {
    if (VSIn.Direction != feme::SignatureDirection::Input ||
        VSIn.SystemValue != feme::SignatureSystemValue::None)
      continue;
    if (!VSIn.Location)
      return createStringError(inconvertibleErrorCode(),
                               "vertex input element %u has no location",
                               VSIn.ElementID);
    bool Found = false;
    for (const VertexInputAttribute &Attr : Attributes)
      Found |= Attr.Location == *VSIn.Location;
    if (!Found)
      return createStringError(inconvertibleErrorCode(),
                               "vertex input location %u has no matching "
                               "VkVertexInputAttributeDescription",
                               *VSIn.Location);
  }
  return Error::success();
}

//===----------------------------------------------------------------------===//
// Render-target identity
//===----------------------------------------------------------------------===//

/// The color attachment formats, sample count, and depth/stencil formats a
/// graphics pipeline is created against: either from its `VkRenderPass` and
/// subpass index, or -- for a dynamic-rendering pipeline -- from the
/// `VkPipelineRenderingCreateInfo` chained onto its create info. Both
/// normalize into the same shape, exactly as the render-target binding
/// itself does at draw time.
struct PipelineRenderTargets {
  std::vector<feme::cpu::ResourceFormat> Colors;
  uint32_t SampleCount = 1;
  std::optional<feme::cpu::ResourceFormat> DepthStencil;
};

const VkPipelineRenderingCreateInfo *findRenderingCreateInfo(const void *Next) {
  for (const auto *Header = static_cast<const VkBaseInStructure *>(Next);
       Header; Header = Header->pNext)
    if (Header->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
      return reinterpret_cast<const VkPipelineRenderingCreateInfo *>(Header);
  return nullptr;
}

Expected<PipelineRenderTargets>
getRenderTargets(const VkGraphicsPipelineCreateInfo &CreateInfo) {
  PipelineRenderTargets Targets;
  if (CreateInfo.renderPass) {
    const RenderPass &Pass = *fromHandle<RenderPass>(CreateInfo.renderPass);
    if (CreateInfo.subpass >= Pass.subpasses().size())
      return createStringError(inconvertibleErrorCode(),
                               "graphics pipeline names subpass %u, which its "
                               "VkRenderPass does not have",
                               CreateInfo.subpass);
    const SubpassDescription &Subpass = Pass.subpasses()[CreateInfo.subpass];
    for (uint32_t Index : Subpass.ColorAttachments) {
      if (Index == VK_ATTACHMENT_UNUSED)
        return createStringError(inconvertibleErrorCode(),
                                 "an unused color attachment slot is not "
                                 "implemented");
      Targets.Colors.push_back(Pass.attachments()[Index].Format);
      Targets.SampleCount = Pass.attachments()[Index].SampleCount;
    }
    if (Subpass.DepthStencilAttachment != VK_ATTACHMENT_UNUSED)
      Targets.DepthStencil =
          Pass.attachments()[Subpass.DepthStencilAttachment].Format;
    return Targets;
  }

  const VkPipelineRenderingCreateInfo *Rendering =
      findRenderingCreateInfo(CreateInfo.pNext);
  if (!Rendering)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs either a VkRenderPass "
                             "or a chained VkPipelineRenderingCreateInfo");
  for (uint32_t I = 0; I != Rendering->colorAttachmentCount; ++I) {
    std::optional<feme::cpu::ResourceFormat> Format =
        mapVkFormat(Rendering->pColorAttachmentFormats[I]);
    if (!Format || !isSupportedColorAttachmentFormat(*Format))
      return createStringError(inconvertibleErrorCode(),
                               "color attachment %u names a format this "
                               "driver cannot render into",
                               I);
    Targets.Colors.push_back(*Format);
  }
  VkFormat DepthStencilFormat =
      Rendering->depthAttachmentFormat != VK_FORMAT_UNDEFINED
          ? Rendering->depthAttachmentFormat
          : Rendering->stencilAttachmentFormat;
  if (DepthStencilFormat != VK_FORMAT_UNDEFINED) {
    std::optional<feme::cpu::ResourceFormat> Format =
        mapVkFormat(DepthStencilFormat);
    if (!Format || (!isSupportedDepthAttachmentFormat(*Format) &&
                    !isSupportedStencilAttachmentFormat(*Format)))
      return createStringError(inconvertibleErrorCode(),
                               "the depth/stencil attachment names a format "
                               "this driver cannot render into");
    Targets.DepthStencil = *Format;
  }
  // Unlike a `VkRenderPass`'s `VkAttachmentDescription::samples` above,
  // `VkPipelineRenderingCreateInfo` carries no sample-count field of its
  // own -- dynamic rendering only ever learns the real render target's
  // sample count at `vkCmdBeginRendering` time (`CommandBuffer.cpp`'s own
  // "the render target's sample count disagrees with the bound pipeline's"
  // check already validates that). The pipeline's own declared
  // `rasterizationSamples` is the only sample count this creation-time
  // code can know for a dynamic-rendering pipeline, so trust it here
  // rather than leaving `Targets.SampleCount` at its single-sample default
  // -- otherwise the check just below would reject every genuinely
  // multisampled dynamic-rendering pipeline as "disagreeing" with a
  // sample count dynamic rendering never actually specified.
  if (CreateInfo.pMultisampleState)
    Targets.SampleCount =
        static_cast<uint32_t>(CreateInfo.pMultisampleState->rasterizationSamples);
  return Targets;
}

//===----------------------------------------------------------------------===//
// Pipeline creation
//===----------------------------------------------------------------------===//

Error translateVertexInput(const VkPipelineVertexInputStateCreateInfo *Info,
                           const VkPhysicalDeviceLimits &Limits,
                           GraphicsPipelineState &Out) {
  if (!Info)
    return Error::success();
  if (Info->vertexBindingDescriptionCount > Limits.maxVertexInputBindings ||
      Info->vertexAttributeDescriptionCount > Limits.maxVertexInputAttributes)
    return createStringError(inconvertibleErrorCode(),
                             "vertex input exceeds maxVertexInputBindings/"
                             "maxVertexInputAttributes");
  for (uint32_t I = 0; I != Info->vertexBindingDescriptionCount; ++I) {
    const VkVertexInputBindingDescription &Src =
        Info->pVertexBindingDescriptions[I];
    if (Src.inputRate != VK_VERTEX_INPUT_RATE_VERTEX &&
        Src.inputRate != VK_VERTEX_INPUT_RATE_INSTANCE)
      return createStringError(inconvertibleErrorCode(),
                               "unknown VkVertexInputRate");
    if (Src.stride > Limits.maxVertexInputBindingStride)
      return createStringError(inconvertibleErrorCode(),
                               "vertex binding stride exceeds "
                               "maxVertexInputBindingStride");
    Out.VertexBindings.push_back(
        VertexInputBinding{Src.binding, Src.stride,
                           Src.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE});
  }
  for (uint32_t I = 0; I != Info->vertexAttributeDescriptionCount; ++I) {
    const VkVertexInputAttributeDescription &Src =
        Info->pVertexAttributeDescriptions[I];
    if (Src.offset > Limits.maxVertexInputAttributeOffset)
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute offset exceeds "
                               "maxVertexInputAttributeOffset");
    std::optional<feme::cpu::ResourceFormat> Format = mapVkFormat(Src.format);
    if (!Format || !isSupportedVertexAttributeFormat(*Format))
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute at location %u names a "
                               "format the vertex fetch cannot decode",
                               Src.location);
    bool HasBinding = false;
    for (const VertexInputBinding &Binding : Out.VertexBindings)
      HasBinding |= Binding.Binding == Src.binding;
    if (!HasBinding)
      return createStringError(inconvertibleErrorCode(),
                               "vertex attribute at location %u names "
                               "binding %u, which the pipeline does not "
                               "declare",
                               Src.location, Src.binding);
    Out.VertexAttributes.push_back(
        VertexInputAttribute{Src.location, Src.binding, Src.offset, *Format});
  }
  // (roadmap F6) `VkPipelineVertexInputDivisorStateCreateInfo`, chained from
  // `pNext`: overrides a per-instance binding's default divisor of 1 with
  // an explicit per-binding value. This is not a new fetch mechanism --
  // the executor's existing per-instance fetch (Executor.cpp) already
  // reads by instance index; a divisor only changes which instance index a
  // fetch of a given instance maps to, and `0`
  // (`vertexAttributeInstanceRateZeroDivisor`) is simply the case where
  // every instance maps to the same one, `firstInstance`.
  for (const VkBaseInStructure *Next =
           reinterpret_cast<const VkBaseInStructure *>(Info->pNext);
       Next; Next = Next->pNext) {
    if (Next->sType !=
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO)
      continue;
    const auto *DivisorState =
        reinterpret_cast<const VkPipelineVertexInputDivisorStateCreateInfo *>(
            Next);
    for (uint32_t I = 0; I != DivisorState->vertexBindingDivisorCount; ++I) {
      const VkVertexInputBindingDivisorDescription &Src =
          DivisorState->pVertexBindingDivisors[I];
      if (Src.divisor > MaxVertexAttribDivisor)
        return createStringError(inconvertibleErrorCode(),
                                 "vertex binding %u's divisor exceeds "
                                 "maxVertexAttribDivisor",
                                 Src.binding);
      VertexInputBinding *Binding = nullptr;
      for (VertexInputBinding &B : Out.VertexBindings)
        if (B.Binding == Src.binding)
          Binding = &B;
      if (!Binding)
        return createStringError(inconvertibleErrorCode(),
                                 "VkVertexInputBindingDivisorDescription "
                                 "names binding %u, which the pipeline does "
                                 "not declare",
                                 Src.binding);
      if (!Binding->PerInstance)
        return createStringError(inconvertibleErrorCode(),
                                 "a vertex binding divisor only applies to "
                                 "a VK_VERTEX_INPUT_RATE_INSTANCE binding");
      Binding->Divisor = Src.divisor;
    }
  }
  return Error::success();
}

Error translateRasterState(const VkPipelineRasterizationStateCreateInfo *Info,
                           GraphicsPipelineState &Out) {
  if (!Info)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs rasterization state");
  if (Info->rasterizerDiscardEnable || Info->depthClampEnable ||
      Info->depthBiasEnable || Info->polygonMode != VK_POLYGON_MODE_FILL)
    return createStringError(inconvertibleErrorCode(),
                             "rasterizer discard, depth clamp, depth bias, "
                             "and non-fill polygon modes are not implemented");
  std::optional<CullMode> Cull = mapCullMode(Info->cullMode);
  if (!Cull)
    return createStringError(inconvertibleErrorCode(),
                             "unrecognized VkCullModeFlags value %u",
                             unsigned(Info->cullMode));
  Out.Raster.Cull = *Cull;
  Out.Raster.Front = Info->frontFace == VK_FRONT_FACE_CLOCKWISE
                         ? FrontFace::Clockwise
                         : FrontFace::CounterClockwise;
  // (roadmap F5) `VK_DYNAMIC_STATE_LINE_WIDTH` is core 1.0, so a pipeline
  // may declare `lineWidth` dynamic with no `VK_KHR_line_rasterization`
  // involvement at all; when it does, `Info->lineWidth` itself is
  // unspecified and must not be read (the same rule every other
  // statically-ignored field in this function already follows).
  if ((Out.DynamicStates & DynamicStateLineWidth) == 0)
    Out.Raster.LineWidth = Info->lineWidth;
  // (roadmap F5) `VkPipelineRasterizationLineStateCreateInfoKHR`, chained
  // from `pNext`: absent entirely, this pipeline keeps `RasterState`'s own
  // default (`Rectangular`, unstippled), exactly matching the spec's
  // documented behavior for `VK_LINE_RASTERIZATION_MODE_DEFAULT_KHR` with
  // stippling disabled.
  for (const VkBaseInStructure *Next =
           reinterpret_cast<const VkBaseInStructure *>(Info->pNext);
       Next; Next = Next->pNext) {
    if (Next->sType !=
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR)
      continue;
    const auto *LineState =
        reinterpret_cast<const VkPipelineRasterizationLineStateCreateInfoKHR *>(
            Next);
    std::optional<LineRasterizationMode> LineMode =
        mapLineRasterizationMode(LineState->lineRasterizationMode);
    if (!LineMode)
      return createStringError(inconvertibleErrorCode(),
                               "unrecognized VkLineRasterizationModeKHR "
                               "value %u",
                               unsigned(LineState->lineRasterizationMode));
    Out.Raster.LineMode = *LineMode;
    Out.Raster.StippledLineEnable = LineState->stippledLineEnable;
    if (LineState->stippledLineEnable) {
      if (LineState->lineStippleFactor < 1 ||
          LineState->lineStippleFactor > 256)
        return createStringError(inconvertibleErrorCode(),
                                 "lineStippleFactor must be in [1, 256]");
      // `vkCmdSetLineStippleKHR`'s dynamic payload replaces both fields
      // together (`VK_DYNAMIC_STATE_LINE_STIPPLE_KHR`), so the static
      // ones are only meaningful when that state stays static, exactly
      // like `lineWidth` above.
      if ((Out.DynamicStates & DynamicStateLineStipple) == 0) {
        Out.Raster.StippleFactor = LineState->lineStippleFactor;
        Out.Raster.StipplePattern = LineState->lineStipplePattern;
      }
    }
    break;
  }
  return Error::success();
}

Error translateDepthStencilState(
    const VkPipelineDepthStencilStateCreateInfo *Info,
    const PipelineRenderTargets &Targets, GraphicsPipelineState &Out) {
  // (roadmap C4c) Whether depth/stencil test/write/op is dynamic: per
  // `VK_EXT_extended_dynamic_state`, a pipeline declaring one of these
  // dynamic must ignore the corresponding static field entirely (its value
  // is unspecified/irrelevant), not merely treat it as an initial value --
  // so neither its boolean fields nor `depthCompareOp`/the stencil op
  // fields may gate whether this function accepts or rejects the
  // pipeline. `DepthBoundsTestEnable` gets the same treatment for the same
  // reason, even though the test itself is never implemented: see
  // `DynamicStateBits`'s own comment on why that combination is still
  // safe to accept.
  bool TestDynamic = (Out.DynamicStates & DynamicStateDepthTestEnable) != 0;
  bool WriteDynamic = (Out.DynamicStates & DynamicStateDepthWriteEnable) != 0;
  bool CompareDynamic = (Out.DynamicStates & DynamicStateDepthCompareOp) != 0;
  bool BoundsDynamic =
      (Out.DynamicStates & DynamicStateDepthBoundsTestEnable) != 0;
  bool StencilTestDynamic =
      (Out.DynamicStates & DynamicStateStencilTestEnable) != 0;
  bool StencilOpDynamic = (Out.DynamicStates & DynamicStateStencilOp) != 0;

  bool NeedsDepth = TestDynamic || WriteDynamic;
  bool NeedsStencil = StencilTestDynamic;
  if (!Info) {
    // A dynamically-enabled test still needs somewhere to test/write into.
    if (NeedsDepth && (!Targets.DepthStencil ||
                       !isSupportedDepthAttachmentFormat(*Targets.DepthStencil)))
      return createStringError(inconvertibleErrorCode(),
                               "depth testing/writes need a depth attachment "
                               "in the pipeline's render target");
    if (NeedsStencil &&
        (!Targets.DepthStencil ||
         !isSupportedStencilAttachmentFormat(*Targets.DepthStencil)))
      return createStringError(inconvertibleErrorCode(),
                               "stencil testing needs an S8_UINT attachment "
                               "in the pipeline's render target");
    return Error::success();
  }
  if (!BoundsDynamic && Info->depthBoundsTestEnable)
    return createStringError(inconvertibleErrorCode(),
                             "the depth bounds test is not implemented");

  NeedsDepth = NeedsDepth || Info->depthTestEnable || Info->depthWriteEnable;
  if (NeedsDepth) {
    if (!Targets.DepthStencil ||
        !isSupportedDepthAttachmentFormat(*Targets.DepthStencil))
      return createStringError(inconvertibleErrorCode(),
                               "depth testing/writes need a depth attachment "
                               "in the pipeline's render target");
    Out.Depth.TestEnable = Info->depthTestEnable != VK_FALSE;
    Out.Depth.WriteEnable = Info->depthWriteEnable != VK_FALSE;
    if (CompareDynamic) {
      // Ignored per the comment above; the resolved value always comes
      // from `DynamicGraphicsState::DepthCompare` instead.
      Out.Depth.Compare = CompareOp::Always;
    } else {
      std::optional<CompareOp> Compare = mapCompareOp(Info->depthCompareOp);
      if (!Compare)
        return createStringError(inconvertibleErrorCode(),
                                 "unrecognized depth compare operation");
      Out.Depth.Compare = *Compare;
    }
  }

  NeedsStencil = NeedsStencil || Info->stencilTestEnable;
  if (!NeedsStencil)
    return Error::success();
  if (!Targets.DepthStencil ||
      !isSupportedStencilAttachmentFormat(*Targets.DepthStencil))
    return createStringError(inconvertibleErrorCode(),
                             "stencil testing needs an S8_UINT attachment in "
                             "the pipeline's render target");
  Out.Stencil.TestEnable = Info->stencilTestEnable != VK_FALSE;
  if (StencilOpDynamic) {
    // `Info->front`/`Info->back`'s op/compare fields are ignored per the
    // comment above; only the reference/compare/write masks (each its own,
    // separately-dynamic state -- `translateDynamicState`'s existing six)
    // still come from here when *they* are static. The op fields
    // themselves always resolve from `DynamicGraphicsState::StencilOps`.
    Out.Stencil.Front.CompareMask =
        static_cast<uint8_t>(Info->front.compareMask);
    Out.Stencil.Front.WriteMask = static_cast<uint8_t>(Info->front.writeMask);
    Out.Stencil.Front.Reference = static_cast<uint8_t>(Info->front.reference);
    Out.Stencil.Back.CompareMask =
        static_cast<uint8_t>(Info->back.compareMask);
    Out.Stencil.Back.WriteMask = static_cast<uint8_t>(Info->back.writeMask);
    Out.Stencil.Back.Reference = static_cast<uint8_t>(Info->back.reference);
    return Error::success();
  }
  auto translateFace = [](const VkStencilOpState &Src,
                          StencilFaceState &Dst) -> Error {
    std::optional<CompareOp> Compare = mapCompareOp(Src.compareOp);
    std::optional<StencilOp> Fail = mapStencilOp(Src.failOp);
    std::optional<StencilOp> DepthFail = mapStencilOp(Src.depthFailOp);
    std::optional<StencilOp> Pass = mapStencilOp(Src.passOp);
    if (!Compare || !Fail || !DepthFail || !Pass)
      return createStringError(inconvertibleErrorCode(),
                               "unrecognized stencil compare/operation");
    Dst.Compare = *Compare;
    Dst.FailOp = *Fail;
    Dst.DepthFailOp = *DepthFail;
    Dst.PassOp = *Pass;
    Dst.CompareMask = static_cast<uint8_t>(Src.compareMask);
    Dst.WriteMask = static_cast<uint8_t>(Src.writeMask);
    Dst.Reference = static_cast<uint8_t>(Src.reference);
    return Error::success();
  };
  if (Error E = translateFace(Info->front, Out.Stencil.Front))
    return E;
  return translateFace(Info->back, Out.Stencil.Back);
}

/// \p HasFragmentStage is false for a pipeline that legally omitted its
/// fragment stage (roadmap H2j, only possible when \p Targets has no color
/// attachments): per the Vulkan spec, a pipeline with no fragment shader
/// has no fragment output interface, so `pColorBlendState` -- including its
/// own `attachmentCount` -- is entirely ignored rather than validated
/// against (the necessarily empty) `Targets.Colors`, exactly like `Info`
/// being null below.
Error translateColorBlendState(const VkPipelineColorBlendStateCreateInfo *Info,
                               const PipelineRenderTargets &Targets,
                               bool HasFragmentStage,
                               GraphicsPipelineState &Out) {
  Out.ColorBlends.assign(Targets.Colors.size(), BlendState{});
  if (!Info || !HasFragmentStage)
    return Error::success();
  if (Info->attachmentCount != Targets.Colors.size())
    return createStringError(inconvertibleErrorCode(),
                             "the pipeline declares %u color blend state(s) "
                             "but its render target has %zu color "
                             "attachment(s)",
                             Info->attachmentCount, Targets.Colors.size());
  if (Info->logicOpEnable) {
    std::optional<LogicOp> Logic = mapLogicOp(Info->logicOp);
    if (!Logic)
      return createStringError(inconvertibleErrorCode(),
                               "unrecognized logic operation");
    Out.LogicOpEnable = true;
    Out.Logic = *Logic;
  }
  for (uint32_t I = 0; I != Info->attachmentCount; ++I) {
    const VkPipelineColorBlendAttachmentState &Src = Info->pAttachments[I];
    BlendState &Dst = Out.ColorBlends[I];
    Dst.BlendEnable = Src.blendEnable != VK_FALSE;
    Dst.WriteMask = static_cast<uint8_t>(Src.colorWriteMask & 0xF);
    if (!Dst.BlendEnable)
      continue;
    std::optional<BlendFactor> SrcColor =
        mapBlendFactor(Src.srcColorBlendFactor);
    std::optional<BlendFactor> DstColor =
        mapBlendFactor(Src.dstColorBlendFactor);
    std::optional<BlendFactor> SrcAlpha =
        mapBlendFactor(Src.srcAlphaBlendFactor);
    std::optional<BlendFactor> DstAlpha =
        mapBlendFactor(Src.dstAlphaBlendFactor);
    std::optional<BlendOp> ColorOp = mapBlendOp(Src.colorBlendOp);
    std::optional<BlendOp> AlphaOp = mapBlendOp(Src.alphaBlendOp);
    if (!SrcColor || !DstColor || !SrcAlpha || !DstAlpha || !ColorOp ||
        !AlphaOp)
      return createStringError(inconvertibleErrorCode(),
                               "color attachment %u names a blend factor or "
                               "operation that is not implemented",
                               I);
    Dst.SrcColorFactor = *SrcColor;
    Dst.DstColorFactor = *DstColor;
    Dst.SrcAlphaFactor = *SrcAlpha;
    Dst.DstAlphaFactor = *DstAlpha;
    Dst.ColorOp = *ColorOp;
    Dst.AlphaOp = *AlphaOp;
  }
  for (unsigned I = 0; I != 4; ++I)
    Out.BlendConstants[I] = Info->blendConstants[I];
  return Error::success();
}

Error translateViewportState(const VkPipelineViewportStateCreateInfo *Info,
                             const VkPhysicalDeviceLimits &Limits,
                             GraphicsPipelineState &Out) {
  if (!Info)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs viewport state");
  // (roadmap C4c) `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`/`_SCISSOR_WITH_
  // COUNT`: per `VK_EXT_extended_dynamic_state`, `Info->viewportCount`/
  // `scissorCount` are ignored (not just an initial value) whenever the
  // matching state is dynamic, so this function must not gate on them --
  // matching the depth/stencil dynamic states' own "ignore the static
  // field entirely" treatment (see `translateDepthStencilState`).
  bool ViewportDynamic = (Out.DynamicStates & DynamicStateViewport) != 0;
  bool ScissorDynamic = (Out.DynamicStates & DynamicStateScissor) != 0;

  auto validateCount = [&](uint32_t Count, const char *Name) -> Error {
    if (Count == 0 || Count > Limits.maxViewports)
      return createStringError(inconvertibleErrorCode(),
                               "%s count %u is out of range for maxViewports "
                               "(%u)",
                               Name, Count, Limits.maxViewports);
    return Error::success();
  };

  if (!ViewportDynamic) {
    if (Error E = validateCount(Info->viewportCount, "viewport"))
      return E;
    if (!Info->pViewports)
      return createStringError(inconvertibleErrorCode(),
                               "static viewport state needs pViewports");
    Out.Viewports.clear();
    Out.Viewports.reserve(Info->viewportCount);
    for (uint32_t I = 0; I != Info->viewportCount; ++I) {
      const VkViewport &Src = Info->pViewports[I];
      if (Src.width > float(Limits.maxViewportDimensions[0]) ||
          Src.height > float(Limits.maxViewportDimensions[1]))
        return createStringError(inconvertibleErrorCode(),
                                 "viewport %u exceeds maxViewportDimensions",
                                 I);
      Out.Viewports.push_back(feme::graphics::ViewportState{
          Src.x, Src.y, Src.width, Src.height, Src.minDepth, Src.maxDepth});
    }
  }
  if (!ScissorDynamic) {
    if (Error E = validateCount(Info->scissorCount, "scissor"))
      return E;
    if (!Info->pScissors)
      return createStringError(inconvertibleErrorCode(),
                               "static scissor state needs pScissors");
    Out.Scissors.clear();
    Out.Scissors.reserve(Info->scissorCount);
    for (uint32_t I = 0; I != Info->scissorCount; ++I) {
      const VkRect2D &Src = Info->pScissors[I];
      Out.Scissors.push_back(feme::graphics::ScissorRect{
          Src.offset.x, Src.offset.y, Src.extent.width, Src.extent.height});
    }
  }
  return Error::success();
}

Error translateDynamicState(const VkPipelineDynamicStateCreateInfo *Info,
                            GraphicsPipelineState &Out) {
  if (!Info)
    return Error::success();
  for (uint32_t I = 0; I != Info->dynamicStateCount; ++I) {
    std::optional<DynamicStateBits> Bit =
        mapDynamicState(Info->pDynamicStates[I]);
    if (!Bit)
      return createStringError(inconvertibleErrorCode(),
                               "dynamic state %u is not implemented",
                               unsigned(Info->pDynamicStates[I]));
    Out.DynamicStates |= *Bit;
  }
  return Error::success();
}

//===----------------------------------------------------------------------===//
// Pipeline cache key
//===----------------------------------------------------------------------===//

/// Appends \p V's raw bytes to \p Out. Only ever called on a scalar
/// (integer, float, or enum) field, never a whole aggregate: an aggregate's
/// inter-member padding is indeterminate for a plain (non-value-initialized)
/// local, and hashing it would make an identical logical pipeline state
/// hash differently run to run.
template <typename T> void appendScalar(std::vector<uint8_t> &Out, T V) {
  const auto *Bytes = reinterpret_cast<const uint8_t *>(&V);
  Out.insert(Out.end(), Bytes, Bytes + sizeof(T));
}

void appendVertexBinding(std::vector<uint8_t> &Out,
                         const VertexInputBinding &B) {
  appendScalar(Out, B.Binding);
  appendScalar(Out, B.Stride);
  appendScalar(Out, B.PerInstance);
  appendScalar(Out, B.Divisor);
}

void appendVertexAttribute(std::vector<uint8_t> &Out,
                           const VertexInputAttribute &A) {
  appendScalar(Out, A.Location);
  appendScalar(Out, A.Binding);
  appendScalar(Out, A.Offset);
  appendScalar(Out, A.Format);
}

void appendStencilFace(std::vector<uint8_t> &Out,
                       const feme::graphics::StencilFaceState &F) {
  appendScalar(Out, F.Compare);
  appendScalar(Out, F.FailOp);
  appendScalar(Out, F.DepthFailOp);
  appendScalar(Out, F.PassOp);
  appendScalar(Out, F.CompareMask);
  appendScalar(Out, F.WriteMask);
  appendScalar(Out, F.Reference);
}

void appendBlendState(std::vector<uint8_t> &Out,
                      const feme::graphics::BlendState &B) {
  appendScalar(Out, B.BlendEnable);
  appendScalar(Out, B.SrcColorFactor);
  appendScalar(Out, B.DstColorFactor);
  appendScalar(Out, B.ColorOp);
  appendScalar(Out, B.SrcAlphaFactor);
  appendScalar(Out, B.DstAlphaFactor);
  appendScalar(Out, B.AlphaOp);
  appendScalar(Out, B.WriteMask);
}

void appendAttachmentFormat(std::vector<uint8_t> &Out,
                            const AttachmentFormat &A) {
  appendScalar(Out, A.Format);
  appendScalar(Out, A.Width);
  appendScalar(Out, A.Height);
}

/// Serializes every piece of \p State a draw through the resulting pipeline
/// could observe, field by field, for `computeGraphicsPipelineCacheKey`
/// (PipelineCache.h): a cache hit must be identical in all of it, not only
/// in the two stages' SPIR-V, since a key covering less than the whole
/// normalized pipeline description is worse than none (see this file's
/// header comment and "Pipeline Cache" in feme/docs/FeMeVulkanDesign.md).
std::vector<uint8_t>
serializeFixedFunctionState(const GraphicsPipelineState &State) {
  std::vector<uint8_t> Out;
  appendScalar(Out, State.Topology);
  appendScalar(Out, State.PrimitiveRestartEnable);
  appendScalar(Out, State.SampleCount);
  appendScalar(Out, State.DynamicStates);
  appendScalar(Out, State.LogicOpEnable);
  appendScalar(Out, State.Logic);
  for (float C : State.BlendConstants)
    appendScalar(Out, C);
  appendScalar(Out, State.Raster.Cull);
  appendScalar(Out, State.Raster.Front);
  appendScalar(Out, State.Depth.TestEnable);
  appendScalar(Out, State.Depth.WriteEnable);
  appendScalar(Out, State.Depth.Compare);
  appendScalar(Out, State.Stencil.TestEnable);
  appendStencilFace(Out, State.Stencil.Front);
  appendStencilFace(Out, State.Stencil.Back);
  appendScalar(Out, State.Viewports.size());
  for (const feme::graphics::ViewportState &Viewport : State.Viewports) {
    appendScalar(Out, Viewport.X);
    appendScalar(Out, Viewport.Y);
    appendScalar(Out, Viewport.Width);
    appendScalar(Out, Viewport.Height);
    appendScalar(Out, Viewport.MinDepth);
    appendScalar(Out, Viewport.MaxDepth);
  }
  appendScalar(Out, State.Scissors.size());
  for (const feme::graphics::ScissorRect &Scissor : State.Scissors) {
    appendScalar(Out, Scissor.X);
    appendScalar(Out, Scissor.Y);
    appendScalar(Out, Scissor.Width);
    appendScalar(Out, Scissor.Height);
  }
  appendScalar(Out, State.VertexBindings.size());
  for (const VertexInputBinding &B : State.VertexBindings)
    appendVertexBinding(Out, B);
  appendScalar(Out, State.VertexAttributes.size());
  for (const VertexInputAttribute &A : State.VertexAttributes)
    appendVertexAttribute(Out, A);
  appendScalar(Out, State.ColorBlends.size());
  for (const feme::graphics::BlendState &B : State.ColorBlends)
    appendBlendState(Out, B);
  appendScalar(Out, State.Attachments.size());
  for (const AttachmentFormat &A : State.Attachments)
    appendAttachmentFormat(Out, A);
  // (roadmap H4b) Only meaningful for a tessellation-enabled pipeline, but
  // cheap to always fold in: a non-tessellating pipeline's
  // `InputControlPointCount` is always the same default value.
  appendScalar(Out, State.Tessellation.InputControlPointCount);
  return Out;
}

/// Translates every piece of `VkGraphicsPipelineCreateInfo` fixed-function
/// state into \p Result (everything but the compiled stages themselves,
/// `Result.Artifact`), and resolves which stage is which: none of it reads
/// the compiled stages, so it runs -- and a pipeline-cache key can be
/// computed from its result -- before paying for stage compilation.
Error translateFixedFunctionState(
    const VkGraphicsPipelineCreateInfo &CreateInfo,
    const PhysicalDeviceInfo &DeviceInfo, GraphicsPipelineState &Result,
    const VkPipelineShaderStageCreateInfo *&VertexInfo,
    const VkPipelineShaderStageCreateInfo *&FragmentInfo,
    const VkPipelineShaderStageCreateInfo *&TessControlInfo,
    const VkPipelineShaderStageCreateInfo *&TessEvalInfo) {
  VertexInfo = nullptr;
  FragmentInfo = nullptr;
  TessControlInfo = nullptr;
  TessEvalInfo = nullptr;
  for (uint32_t I = 0; I != CreateInfo.stageCount; ++I) {
    const VkPipelineShaderStageCreateInfo &Stage = CreateInfo.pStages[I];
    switch (Stage.stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
      VertexInfo = &Stage;
      break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
      FragmentInfo = &Stage;
      break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      TessControlInfo = &Stage;
      break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      TessEvalInfo = &Stage;
      break;
    default:
      // Geometry, mesh and task stages are later milestones (V7/V8); a
      // pipeline naming one fails here rather than rendering without it.
      return createStringError(inconvertibleErrorCode(),
                               "only the vertex, fragment and tessellation "
                               "stages are implemented (V6/H4b)");
    }
  }
  if (!VertexInfo)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs a vertex stage");
  // (roadmap H4b) The two tessellation stages are only ever legal together:
  // a hull shader with no domain shader (or vice versa) has no tessellator
  // state to run with (`VUID-VkGraphicsPipelineCreateInfo-pStages-00736`/
  // neighbors).
  if ((TessControlInfo != nullptr) != (TessEvalInfo != nullptr))
    return createStringError(
        inconvertibleErrorCode(),
        "a graphics pipeline naming a tessellation-control stage needs a "
        "tessellation-evaluation stage too, and vice versa");

  // (roadmap F10) `VK_EXT_pipeline_robustness`: each stage's own
  // `VkPipelineRobustnessCreateInfo` (falling back to the pipeline-level
  // one, see `PipelineRobustness`'s own comment) is resolved and validated
  // independently, since the extension's own spec text scopes it "to all
  // accesses emanating from the shader code of this shader stage". A
  // fragment-less pipeline (below) has no fragment-stage `pNext` to
  // resolve, so `Result.FragmentRobustness` is left at its default.
  Expected<PipelineRobustness> VertexRobustness =
      resolvePipelineRobustness(CreateInfo.pNext, VertexInfo->pNext);
  if (!VertexRobustness)
    return VertexRobustness.takeError();
  Result.VertexRobustness = *VertexRobustness;
  if (FragmentInfo) {
    Expected<PipelineRobustness> FragmentRobustness =
        resolvePipelineRobustness(CreateInfo.pNext, FragmentInfo->pNext);
    if (!FragmentRobustness)
      return FragmentRobustness.takeError();
    Result.FragmentRobustness = *FragmentRobustness;
  }

  Expected<PipelineRenderTargets> Targets = getRenderTargets(CreateInfo);
  if (!Targets)
    return Targets.takeError();
  // A depth-only render target -- no color attachments at all -- is legal
  // Vulkan (`dEQP-VK.multiview.depth_without_fragment_shader`'s own shape,
  // roadmap H2b); nothing below this point assumes a nonempty `Colors`.
  if (Targets->Colors.size() > DeviceInfo.Properties.limits.maxColorAttachments)
    return createStringError(inconvertibleErrorCode(),
                             "the render target exceeds maxColorAttachments");
  // (roadmap H2j) A fragment shader is only genuinely optional when the
  // render target has no color attachments at all
  // (`VUID-VkGraphicsPipelineCreateInfo-pStages-06894`/neighbors); a
  // color-attached pipeline still requires one to produce those outputs.
  if (!FragmentInfo && !Targets->Colors.empty())
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline with color attachments "
                             "needs a fragment stage");

  const VkPipelineInputAssemblyStateCreateInfo *InputAssembly =
      CreateInfo.pInputAssemblyState;
  if (!InputAssembly)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs input assembly state");
  if (InputAssembly->primitiveRestartEnable &&
      InputAssembly->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
    return createStringError(inconvertibleErrorCode(),
                             "primitive restart is only implemented for "
                             "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP");
  std::optional<PrimitiveTopology> Topology =
      mapTopology(InputAssembly->topology);
  if (!Topology)
    return createStringError(inconvertibleErrorCode(),
                             "primitive topology %u is not implemented",
                             unsigned(InputAssembly->topology));
  // (roadmap H4b) A tessellation-enabled pipeline must use
  // `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` -- it is the only topology the
  // tessellator can patch-assemble from -- and, symmetrically, that
  // topology is meaningless without a tessellator to feed it to
  // (`VUID-VkGraphicsPipelineCreateInfo-topology-08889`/neighbors).
  bool HasTessellationStages = TessControlInfo != nullptr;
  if (HasTessellationStages != (*Topology == PrimitiveTopology::PatchList))
    return createStringError(
        inconvertibleErrorCode(),
        "VK_PRIMITIVE_TOPOLOGY_PATCH_LIST requires a tessellation-control/"
        "evaluation stage pair, and vice versa");

  if (HasTessellationStages) {
    const VkPipelineTessellationStateCreateInfo *Tessellation =
        CreateInfo.pTessellationState;
    if (!Tessellation)
      return createStringError(
          inconvertibleErrorCode(),
          "a tessellation-enabled graphics pipeline needs "
          "VkPipelineTessellationStateCreateInfo");
    if (Tessellation->patchControlPoints == 0 ||
        Tessellation->patchControlPoints >
            DeviceInfo.Properties.limits.maxTessellationPatchSize)
      return createStringError(
          inconvertibleErrorCode(),
          "patchControlPoints %u exceeds maxTessellationPatchSize %u",
          Tessellation->patchControlPoints,
          DeviceInfo.Properties.limits.maxTessellationPatchSize);
    Result.Tessellation.InputControlPointCount =
        Tessellation->patchControlPoints;
  }

  Result.Topology = *Topology;
  Result.PrimitiveRestartEnable = InputAssembly->primitiveRestartEnable;
  Result.SampleCount = Targets->SampleCount;

  const VkPhysicalDeviceLimits &Limits = DeviceInfo.Properties.limits;
  // Dynamic state is translated first: `translateDepthStencilState` below
  // needs to know whether `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE` was
  // declared before it can decide whether the static
  // `depthBoundsTestEnable` field is meaningful (see that function's own
  // comment).
  if (Error E = translateDynamicState(CreateInfo.pDynamicState, Result))
    return E;
  if (Error E =
          translateVertexInput(CreateInfo.pVertexInputState, Limits, Result))
    return E;
  if (Error E = translateRasterState(CreateInfo.pRasterizationState, Result))
    return E;
  if (Error E =
          translateViewportState(CreateInfo.pViewportState, Limits, Result))
    return E;
  if (Error E = translateDepthStencilState(CreateInfo.pDepthStencilState,
                                           *Targets, Result))
    return E;
  if (Error E = translateColorBlendState(CreateInfo.pColorBlendState, *Targets,
                                         FragmentInfo != nullptr, Result))
    return E;

  if (const VkPipelineMultisampleStateCreateInfo *Multisample =
          CreateInfo.pMultisampleState) {
    uint32_t Samples = static_cast<uint32_t>(Multisample->rasterizationSamples);
    if (!isSupportedAttachmentSampleCount(Samples))
      return createStringError(inconvertibleErrorCode(),
                               "rasterization sample count %u is not "
                               "implemented (1, 2, 4 and 8 are)",
                               Samples);
    if (Samples != Targets->SampleCount)
      return createStringError(inconvertibleErrorCode(),
                               "the pipeline's rasterization sample count "
                               "disagrees with its render target's");
    if (Multisample->sampleShadingEnable ||
        Multisample->alphaToCoverageEnable || Multisample->alphaToOneEnable)
      return createStringError(inconvertibleErrorCode(),
                               "sample shading, alpha-to-coverage and "
                               "alpha-to-one are not implemented");
    if (Multisample->pSampleMask && Samples <= 32 &&
        (*Multisample->pSampleMask & ((1u << Samples) - 1)) !=
            ((1u << Samples) - 1))
      return createStringError(inconvertibleErrorCode(),
                               "a partial VkSampleMask is not implemented");
  }

  // A pipeline's attachment identity is its formats; the extent is a
  // per-draw property of the render-target binding, and the executor reads
  // this list as cache identity only.
  for (feme::cpu::ResourceFormat Format : Targets->Colors)
    Result.Attachments.push_back(AttachmentFormat{Format, 0, 0});
  return Error::success();
}

/// Compiles both stages, validates them against \p Layout and each other,
/// and builds the shareable artifact -- everything a pipeline-cache miss
/// still has to do that a hit skips entirely.
///
/// \p FragmentInfo is `nullptr` for a pipeline that legally omitted its
/// fragment stage (roadmap H2j): fragment-stage compilation, its root-
/// constant/bound-range validation, and its half of cross-stage interface
/// validation are all skipped in that case, and the returned artifact's own
/// `FragmentStage` is left `nullptr` too.
///
/// \p TessControlInfo/\p TessEvalInfo are `nullptr` together for a
/// pipeline with no tessellation stages (roadmap H4b) -- see
/// `translateFixedFunctionState`'s "both or neither" check -- in which case
/// the returned artifact's `HullStage`/`PatchConstantStage`/`DomainStage`
/// are all left `nullptr` too, and \p Tessellation (already carrying
/// `InputControlPointCount` from `translateFixedFunctionState`) is left
/// otherwise untouched. Otherwise, the tessellation-control module is
/// compiled twice -- once selecting its own entry point for the
/// control-point phase (`ShaderStage::Hull`), once selecting the
/// `<entry>.patchconstant` entry `feme::graphics::
/// splitTessellationControlEntry` (CanonicalizeStage.cpp) split out of it
/// for the patch-constant phase (also `ShaderStage::Hull`; the two phases
/// are only ever told apart by entry-point name, see
/// `compileGraphicsStage`'s own comment) -- and the tessellation-evaluation
/// module is compiled once for the domain phase (`ShaderStage::Domain`),
/// with \p Tessellation filled in from all three compiles' own
/// `feme.tessellation.*` reflection and validated once complete.
Expected<std::shared_ptr<GraphicsPipelineArtifact>> compileAndValidateStages(
    const VkPipelineShaderStageCreateInfo &VertexInfo,
    const VkPipelineShaderStageCreateInfo *FragmentInfo,
    const VkPipelineShaderStageCreateInfo *TessControlInfo,
    const VkPipelineShaderStageCreateInfo *TessEvalInfo,
    const PipelineLayout &Layout, const VkPhysicalDeviceLimits &Limits,
    uint32_t ColorAttachmentCount,
    llvm::ArrayRef<VertexInputAttribute> VertexAttributes,
    feme::graphics::TessellationState &Tessellation) {
  auto Ctx = std::make_unique<feme::Context>();
  Ctx->setDiagnosticHandler([](const feme::Diagnostic &) {});

  Expected<std::shared_ptr<feme::cpu::CompiledStage>> VertexStage =
      compileGraphicsStage(*Ctx, VertexInfo, feme::ShaderStage::Vertex);
  if (!VertexStage)
    return VertexStage.takeError();
  std::shared_ptr<feme::cpu::CompiledStage> FragmentStage;
  if (FragmentInfo) {
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> Compiled =
        compileGraphicsStage(*Ctx, *FragmentInfo, feme::ShaderStage::Fragment);
    if (!Compiled)
      return Compiled.takeError();
    FragmentStage = std::move(*Compiled);
  }

  std::shared_ptr<feme::cpu::CompiledStage> HullStage;
  std::shared_ptr<feme::cpu::CompiledStage> PatchConstantStage;
  std::shared_ptr<feme::cpu::CompiledStage> DomainStage;
  if (TessControlInfo) {
    std::string ControlEntry =
        TessControlInfo->pName ? TessControlInfo->pName : "main";
    std::string PatchConstantEntry = ControlEntry + ".patchconstant";

    std::optional<feme::graphics::TessellationState> ControlPointState;
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> HullCompiled =
        compileGraphicsStage(*Ctx, *TessControlInfo, feme::ShaderStage::Hull,
                             ControlEntry, &ControlPointState);
    if (!HullCompiled)
      return HullCompiled.takeError();
    HullStage = std::move(*HullCompiled);

    Expected<std::shared_ptr<feme::cpu::CompiledStage>> PatchConstantCompiled =
        compileGraphicsStage(*Ctx, *TessControlInfo, feme::ShaderStage::Hull,
                             PatchConstantEntry);
    if (!PatchConstantCompiled)
      return PatchConstantCompiled.takeError();
    PatchConstantStage = std::move(*PatchConstantCompiled);

    std::optional<feme::graphics::TessellationState> DomainState;
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> DomainCompiled =
        compileGraphicsStage(*Ctx, *TessEvalInfo, feme::ShaderStage::Domain,
                             {}, &DomainState);
    if (!DomainCompiled)
      return DomainCompiled.takeError();
    DomainStage = std::move(*DomainCompiled);

    // (roadmap H4b) Each half's own `feme.tessellation.*` reflection is
    // independently optional (see `feme::graphics::getTessellationState`'s
    // own comment); a module that failed to set the execution mode(s) its
    // half is responsible for has nothing sensible to merge in here, and
    // fails now rather than tessellating with silently-defaulted state.
    if (!ControlPointState)
      return createStringError(
          inconvertibleErrorCode(),
          "the tessellation-control stage's entry point '%s' declares no "
          "OutputVertices execution mode",
          ControlEntry.c_str());
    if (!DomainState)
      return createStringError(
          inconvertibleErrorCode(),
          "the tessellation-evaluation stage declares no tessellation "
          "domain execution mode (Triangles/Quads/Isolines)");
    Tessellation.OutputControlPointCount =
        ControlPointState->OutputControlPointCount;
    Tessellation.Domain = DomainState->Domain;
    Tessellation.Partitioning = DomainState->Partitioning;
    Tessellation.OutputPrimitive = DomainState->OutputPrimitive;

    std::string ValidationError;
    llvm::raw_string_ostream ErrOS(ValidationError);
    if (!feme::graphics::validatePatchControlPointCounts(
            Tessellation.InputControlPointCount,
            Tessellation.OutputControlPointCount, &ErrOS)) {
      ErrOS.flush();
      return createStringError(inconvertibleErrorCode(), "%s",
                               ValidationError.c_str());
    }
  }

  const feme::cpu::ResourceInfo &VSInfo = (*VertexStage)->getResourceInfo();
  if (!pushConstantsCoverRootConstantSize(Layout, VSInfo.RootConstantSize,
                                          Limits.maxPushConstantsSize,
                                          VK_SHADER_STAGE_VERTEX_BIT))
    return createStringError(
        inconvertibleErrorCode(),
        "a stage's root-constant span is not fully covered by a "
        "VkPushConstantRange visible to it in its VkPipelineLayout");
  if (Error E = validateBoundRanges(VSInfo, Layout))
    return std::move(E);
  if (FragmentStage) {
    const feme::cpu::ResourceInfo &FSInfo = FragmentStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, FSInfo.RootConstantSize,
                                            Limits.maxPushConstantsSize,
                                            VK_SHADER_STAGE_FRAGMENT_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(FSInfo, Layout))
      return std::move(E);
  }
  if (HullStage) {
    const feme::cpu::ResourceInfo &HSInfo = HullStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(
            Layout, HSInfo.RootConstantSize, Limits.maxPushConstantsSize,
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(HSInfo, Layout))
      return std::move(E);
    const feme::cpu::ResourceInfo &DSInfo = DomainStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(
            Layout, DSInfo.RootConstantSize, Limits.maxPushConstantsSize,
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(DSInfo, Layout))
      return std::move(E);
  }

  if (Error E = validateStageInterfaces(**VertexStage, FragmentStage.get(),
                                        DomainStage.get(),
                                        ColorAttachmentCount, VertexAttributes))
    return std::move(E);

  auto Artifact = std::make_shared<GraphicsPipelineArtifact>();
  Artifact->Ctx = std::move(Ctx);
  Artifact->VertexStage = std::move(*VertexStage);
  Artifact->FragmentStage = std::move(FragmentStage);
  Artifact->HullStage = std::move(HullStage);
  Artifact->PatchConstantStage = std::move(PatchConstantStage);
  Artifact->DomainStage = std::move(DomainStage);
  return Artifact;
}

Expected<std::optional<GraphicsPipelineState>>
compileGraphicsPipeline(const VkGraphicsPipelineCreateInfo &CreateInfo,
                        const PhysicalDeviceInfo &DeviceInfo,
                        PipelineCache *Cache, bool &CacheHit) {
  CacheHit = false;
  if (!CreateInfo.layout)
    return createStringError(inconvertibleErrorCode(),
                             "graphics pipeline requires a VkPipelineLayout");

  GraphicsPipelineState Result;
  const VkPipelineShaderStageCreateInfo *VertexInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *FragmentInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *TessControlInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *TessEvalInfo = nullptr;
  if (Error E =
          translateFixedFunctionState(CreateInfo, DeviceInfo, Result,
                                      VertexInfo, FragmentInfo,
                                      TessControlInfo, TessEvalInfo))
    return std::move(E);

  const PipelineLayout &Layout = *fromHandle<PipelineLayout>(CreateInfo.layout);
  auto *VertexModule = fromHandle<ShaderModule>(VertexInfo->module);
  // (roadmap H2j) `FragmentInfo` is `nullptr` for a pipeline that legally
  // omitted its fragment stage; `FragmentModule` (and the words/entry point
  // fed into the cache key below) follow it to a null/empty state rather
  // than dereferencing a stage that was never named.
  auto *FragmentModule =
      FragmentInfo ? fromHandle<ShaderModule>(FragmentInfo->module) : nullptr;
  // (roadmap H4b) `TessControlInfo`/`TessEvalInfo` are `nullptr` together
  // for a pipeline with no tessellation stages; the modules (and the
  // words/entry points fed into the cache key below) follow them to a
  // null/empty state the same way.
  auto *TessControlModule =
      TessControlInfo ? fromHandle<ShaderModule>(TessControlInfo->module)
                      : nullptr;
  auto *TessEvalModule =
      TessEvalInfo ? fromHandle<ShaderModule>(TessEvalInfo->module) : nullptr;

  // A cache key needs the whole normalized pipeline description (see
  // "Pipeline Cache" in feme/docs/FeMeVulkanDesign.md): computed here, it
  // can be checked *before* paying for stage compilation, unlike a key
  // computed from the compiled result.
  std::optional<PipelineCacheKey> Key;
  if (Cache && VertexModule && (!FragmentInfo || FragmentModule) &&
      (!TessControlInfo || (TessControlModule && TessEvalModule))) {
    std::vector<uint8_t> FixedFunctionState =
        serializeFixedFunctionState(Result);
    llvm::ArrayRef<uint32_t> FragmentWords =
        FragmentModule ? FragmentModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef FragmentEntry =
        FragmentInfo ? (FragmentInfo->pName ? FragmentInfo->pName : "main")
                     : llvm::StringRef();
    llvm::ArrayRef<uint32_t> TessControlWords =
        TessControlModule ? TessControlModule->words()
                          : llvm::ArrayRef<uint32_t>();
    llvm::StringRef TessControlEntry =
        TessControlInfo
            ? (TessControlInfo->pName ? TessControlInfo->pName : "main")
            : llvm::StringRef();
    llvm::ArrayRef<uint32_t> TessEvalWords =
        TessEvalModule ? TessEvalModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef TessEvalEntry =
        TessEvalInfo ? (TessEvalInfo->pName ? TessEvalInfo->pName : "main")
                    : llvm::StringRef();
    Key = computeGraphicsPipelineCacheKey(
        DeviceInfo.Properties.pipelineCacheUUID, VertexModule->words(),
        VertexInfo->pName ? VertexInfo->pName : "main", FragmentWords,
        FragmentEntry, Layout.setLayouts(), Layout.pushConstantRanges(),
        FixedFunctionState, TessControlWords, TessControlEntry, TessEvalWords,
        TessEvalEntry);
  }

  std::shared_ptr<GraphicsPipelineArtifact> Artifact =
      Key ? Cache->lookupGraphics(*Key) : nullptr;
  CacheHit = Artifact != nullptr;
  if (!Artifact) {
    // (roadmap E9) `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_
    // BIT`: this pipeline missed the cache (or none was given), and the
    // caller asked to be told rather than pay for a real compile here.
    if (CreateInfo.flags &
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return std::nullopt;
    Expected<std::shared_ptr<GraphicsPipelineArtifact>> Compiled =
        compileAndValidateStages(
            *VertexInfo, FragmentInfo, TessControlInfo, TessEvalInfo, Layout,
            DeviceInfo.Properties.limits,
            static_cast<uint32_t>(Result.Attachments.size()),
            Result.VertexAttributes, Result.Tessellation);
    if (!Compiled)
      return Compiled.takeError();
    Artifact = std::move(*Compiled);
    if (Key)
      Cache->insertGraphics(*Key, Artifact);
  }
  Result.Artifact = std::move(Artifact);
  return Result;
}

} // namespace

namespace feme::vulkan {

feme::graphics::GraphicsPipeline GraphicsPipeline::buildExecutorPipeline(
    const DynamicGraphicsState &Dynamic) const {
  feme::graphics::StencilState ResolvedStencil = State.Stencil;
  feme::graphics::StencilFaceState *Resolved[2] = {&ResolvedStencil.Front,
                                                   &ResolvedStencil.Back};
  for (unsigned I = 0; I != 2; ++I) {
    if (isDynamic(DynamicStateStencilReference))
      Resolved[I]->Reference =
          static_cast<uint8_t>(Dynamic.StencilReference[I]);
    if (isDynamic(DynamicStateStencilCompareMask))
      Resolved[I]->CompareMask =
          static_cast<uint8_t>(Dynamic.StencilCompareMask[I]);
    if (isDynamic(DynamicStateStencilWriteMask))
      Resolved[I]->WriteMask =
          static_cast<uint8_t>(Dynamic.StencilWriteMask[I]);
    if (isDynamic(DynamicStateStencilOp)) {
      const DynamicGraphicsState::StencilOpState &Op = Dynamic.StencilOps[I];
      Resolved[I]->FailOp = Op.FailOp;
      Resolved[I]->PassOp = Op.PassOp;
      Resolved[I]->DepthFailOp = Op.DepthFailOp;
      Resolved[I]->Compare = Op.Compare;
    }
  }
  if (isDynamic(DynamicStateStencilTestEnable))
    ResolvedStencil.TestEnable = Dynamic.StencilTestEnable;

  feme::graphics::RasterState ResolvedRaster = State.Raster;
  if (isDynamic(DynamicStateCullMode))
    ResolvedRaster.Cull = Dynamic.Cull;
  if (isDynamic(DynamicStateFrontFace))
    ResolvedRaster.Front = Dynamic.Front;
  if (isDynamic(DynamicStateLineWidth))
    ResolvedRaster.LineWidth = Dynamic.LineWidth;
  if (isDynamic(DynamicStateLineStipple)) {
    ResolvedRaster.StippleFactor = Dynamic.StippleFactor;
    ResolvedRaster.StipplePattern = Dynamic.StipplePattern;
  }

  feme::graphics::DepthState ResolvedDepth = State.Depth;
  if (isDynamic(DynamicStateDepthTestEnable))
    ResolvedDepth.TestEnable = Dynamic.DepthTestEnable;
  if (isDynamic(DynamicStateDepthWriteEnable))
    ResolvedDepth.WriteEnable = Dynamic.DepthWriteEnable;
  if (isDynamic(DynamicStateDepthCompareOp))
    ResolvedDepth.Compare = Dynamic.DepthCompare;
  // `Dynamic.DepthBoundsTestEnable` is intentionally never read here: see
  // `DynamicStateBits`'s comment on why it can only ever be `VK_FALSE`.

  feme::graphics::PrimitiveTopology ResolvedTopology =
      (isDynamic(DynamicStatePrimitiveTopology) && Dynamic.Topology)
          ? *Dynamic.Topology
          : State.Topology;

  feme::graphics::GraphicsPipeline Result(
      State.Artifact->VertexStage, State.Artifact->FragmentStage,
      ResolvedTopology, ResolvedRaster, ResolvedDepth,
      feme::graphics::BlendMode::Replace, State.SampleCount, State.Attachments,
      ResolvedStencil, State.ColorBlends, State.LogicOpEnable, State.Logic,
      isDynamic(DynamicStateBlendConstants) ? Dynamic.BlendConstants
                                            : State.BlendConstants,
      State.PrimitiveRestartEnable);
  // (roadmap H4b) `Artifact->HullStage` is set exactly when this pipeline
  // declared tessellation stages (see `compileAndValidateStages`'s own
  // comment); `PatchConstantStage`/`DomainStage` are always set alongside
  // it, and `State.Tessellation` was filled in and validated there too.
  if (State.Artifact->HullStage)
    Result.setTessellationStages(
        State.Artifact->HullStage, State.Artifact->PatchConstantStage,
        State.Artifact->DomainStage, State.Tessellation);
  return Result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
  const PhysicalDeviceInfo &DeviceInfo =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  auto *Cache =
      pipelineCache ? fromHandle<PipelineCache>(pipelineCache) : nullptr;
  Allocator Alloc(pAllocator);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != createInfoCount; ++I) {
    pPipelines[I] = VK_NULL_HANDLE;
    bool CacheHit = false;
    Expected<std::optional<GraphicsPipelineState>> Compiled =
        compileGraphicsPipeline(pCreateInfos[I], DeviceInfo, Cache, CacheHit);
    if (!Compiled) {
      logCreationFailure(Compiled.takeError(), "vkCreateGraphicsPipelines");
      Result = VK_ERROR_INITIALIZATION_FAILED;
      continue;
    }
    if (!*Compiled) {
      // (roadmap E9) A cache miss with
      // `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` set --
      // see `compileGraphicsPipeline`'s own comment. Not an error a more
      // severe one (a real compile failure, or out-of-memory below) should
      // ever be masked by.
      if (Result == VK_SUCCESS)
        Result = VK_PIPELINE_COMPILE_REQUIRED;
      continue;
    }
    // (roadmap E19) `VK_EXT_pipeline_creation_feedback`: one feedback slot
    // per `pStages` entry.
    fillPipelineCreationFeedback(pCreateInfos[I].pNext,
                                 pCreateInfos[I].stageCount, CacheHit);
    GraphicsPipeline *Obj = Alloc.create<GraphicsPipeline>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(**Compiled),
        pCreateInfos[I].flags);
    if (!Obj) {
      Result = VK_ERROR_OUT_OF_HOST_MEMORY;
      continue;
    }
    pPipelines[I] = toHandle<VkPipeline>(static_cast<Pipeline *>(Obj));
  }
  return Result;
}

} // namespace feme::vulkan
