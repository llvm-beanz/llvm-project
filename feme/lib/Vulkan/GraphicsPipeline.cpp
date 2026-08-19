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
#include "PipelineCache.h"
#include "RenderPass.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
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
using feme::graphics::LogicOp;
using feme::graphics::PrimitiveTopology;
using feme::graphics::StencilFaceState;
using feme::graphics::StencilOp;

//===----------------------------------------------------------------------===//
// Fixed-function state translation
//===----------------------------------------------------------------------===//

std::optional<PrimitiveTopology> mapTopology(VkPrimitiveTopology Topology) {
  switch (Topology) {
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    return PrimitiveTopology::TriangleList;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    return PrimitiveTopology::TriangleStrip;
  default:
    // Point/line topologies have no rasterization path yet, and the
    // adjacency ones need a geometry stage (V7): "validated against the
    // advertised topologies".
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
  default:
    // The dual-source factors have no `BlendFactor` peer: no fragment stage
    // writes a second output yet.
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
  // `DynamicGraphicsState::Viewport` whenever `DynamicStateViewport` is
  // set, and `vkCmdSetViewportWithCountEXT` (this ICD's `maxViewports ==
  // 1`, so "with count" carries no more information than the fixed-count
  // command) writes into that same field.
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
Expected<std::shared_ptr<feme::cpu::CompiledStage>>
compileGraphicsStage(feme::Context &Ctx,
                     const VkPipelineShaderStageCreateInfo &StageInfo,
                     feme::ShaderStage Stage) {
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
  std::string EntryPoint = StageInfo.pName ? StageInfo.pName : "main";

  Expected<feme::Module> AsLLVMIR = importShaderModule(Ctx, Module->words());
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  ModuleAnalysisManager MAM;
  feme::graphics::CanonicalizeStagePass().run(AsLLVMIR->getLLVMModule(), MAM);

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
/// interface obligations the executor itself has -- an `SV_Position` vertex
/// output and one `SV_TargetN` fragment output per color attachment -- here,
/// at creation, rather than leaving them for the first draw.
Error validateStageInterfaces(const feme::cpu::CompiledStage &VertexStage,
                              const feme::cpu::CompiledStage &FragmentStage,
                              uint32_t ColorAttachmentCount,
                              llvm::ArrayRef<VertexInputAttribute> Attributes) {
  Expected<feme::EntrySignature> VSSig = getStageSignature(VertexStage);
  if (!VSSig)
    return VSSig.takeError();
  Expected<feme::EntrySignature> FSSig = getStageSignature(FragmentStage);
  if (!FSSig)
    return FSSig.takeError();

  const feme::SignatureElement *Position =
      findSystemValue(*VSSig, feme::SignatureDirection::Output,
                      feme::SignatureSystemValue::Position);
  if (!Position || Position->ComponentCount != 4)
    return createStringError(inconvertibleErrorCode(),
                             "vertex stage does not write a 4-component "
                             "SV_Position output");

  for (const feme::SignatureElement &FSIn : FSSig->Elements) {
    if (FSIn.Direction != feme::SignatureDirection::Input ||
        FSIn.SystemValue != feme::SignatureSystemValue::None)
      continue;
    if (!FSIn.Location)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input element %u has no location to "
                               "link against a vertex output",
                               FSIn.ElementID);
    const feme::SignatureElement *VSOut =
        findLocation(*VSSig, feme::SignatureDirection::Output, *FSIn.Location);
    if (!VSOut)
      return createStringError(inconvertibleErrorCode(),
                               "fragment input location %u has no matching "
                               "vertex stage output",
                               *FSIn.Location);
    if (VSOut->ComponentCount != FSIn.ComponentCount ||
        VSOut->ComponentType != FSIn.ComponentType)
      return createStringError(inconvertibleErrorCode(),
                               "vertex output and fragment input at location "
                               "%u disagree on component count/type",
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

Error translateColorBlendState(const VkPipelineColorBlendStateCreateInfo *Info,
                               const PipelineRenderTargets &Targets,
                               GraphicsPipelineState &Out) {
  Out.ColorBlends.assign(Targets.Colors.size(), BlendState{});
  if (!Info)
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
  // Multiple viewports/scissors need viewport array indexing (V7); a
  // pipeline declaring more than one is rejected rather than silently
  // rasterized through the first.
  if ((!ViewportDynamic && Info->viewportCount != 1) ||
      (!ScissorDynamic && Info->scissorCount != 1))
    return createStringError(inconvertibleErrorCode(),
                             "exactly one viewport and one scissor are "
                             "implemented (maxViewports is 1)");
  if (Info->pViewports) {
    const VkViewport &Src = *Info->pViewports;
    if (Src.width > float(Limits.maxViewportDimensions[0]) ||
        Src.height > float(Limits.maxViewportDimensions[1]))
      return createStringError(inconvertibleErrorCode(),
                               "viewport exceeds maxViewportDimensions");
    Out.Viewport = feme::graphics::ViewportState{
        Src.x, Src.y, Src.width, Src.height, Src.minDepth, Src.maxDepth};
  }
  if (Info->pScissors) {
    const VkRect2D &Src = *Info->pScissors;
    Out.Scissor = feme::graphics::ScissorRect{
        Src.offset.x, Src.offset.y, Src.extent.width, Src.extent.height};
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
  appendScalar(Out, State.Viewport.X);
  appendScalar(Out, State.Viewport.Y);
  appendScalar(Out, State.Viewport.Width);
  appendScalar(Out, State.Viewport.Height);
  appendScalar(Out, State.Viewport.MinDepth);
  appendScalar(Out, State.Viewport.MaxDepth);
  appendScalar(Out, State.Scissor.X);
  appendScalar(Out, State.Scissor.Y);
  appendScalar(Out, State.Scissor.Width);
  appendScalar(Out, State.Scissor.Height);
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
    const VkPipelineShaderStageCreateInfo *&FragmentInfo) {
  VertexInfo = nullptr;
  FragmentInfo = nullptr;
  for (uint32_t I = 0; I != CreateInfo.stageCount; ++I) {
    const VkPipelineShaderStageCreateInfo &Stage = CreateInfo.pStages[I];
    switch (Stage.stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
      VertexInfo = &Stage;
      break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
      FragmentInfo = &Stage;
      break;
    default:
      // Tessellation, geometry, mesh and task stages are later milestones
      // (V7/V8); a pipeline naming one fails here rather than rendering
      // without it.
      return createStringError(inconvertibleErrorCode(),
                               "only the vertex and fragment stages are "
                               "implemented (V6)");
    }
  }
  if (!VertexInfo || !FragmentInfo)
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs both a vertex and a "
                             "fragment stage");

  Expected<PipelineRenderTargets> Targets = getRenderTargets(CreateInfo);
  if (!Targets)
    return Targets.takeError();
  if (Targets->Colors.empty())
    return createStringError(inconvertibleErrorCode(),
                             "a graphics pipeline needs at least one color "
                             "attachment");
  if (Targets->Colors.size() > DeviceInfo.Properties.limits.maxColorAttachments)
    return createStringError(inconvertibleErrorCode(),
                             "the render target exceeds maxColorAttachments");

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
                                         Result))
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
Expected<std::shared_ptr<GraphicsPipelineArtifact>> compileAndValidateStages(
    const VkPipelineShaderStageCreateInfo &VertexInfo,
    const VkPipelineShaderStageCreateInfo &FragmentInfo,
    const PipelineLayout &Layout, const VkPhysicalDeviceLimits &Limits,
    uint32_t ColorAttachmentCount,
    llvm::ArrayRef<VertexInputAttribute> VertexAttributes) {
  auto Ctx = std::make_unique<feme::Context>();
  Ctx->setDiagnosticHandler([](const feme::Diagnostic &) {});

  Expected<std::shared_ptr<feme::cpu::CompiledStage>> VertexStage =
      compileGraphicsStage(*Ctx, VertexInfo, feme::ShaderStage::Vertex);
  if (!VertexStage)
    return VertexStage.takeError();
  Expected<std::shared_ptr<feme::cpu::CompiledStage>> FragmentStage =
      compileGraphicsStage(*Ctx, FragmentInfo, feme::ShaderStage::Fragment);
  if (!FragmentStage)
    return FragmentStage.takeError();

  const feme::cpu::ResourceInfo &VSInfo = (*VertexStage)->getResourceInfo();
  const feme::cpu::ResourceInfo &FSInfo = (*FragmentStage)->getResourceInfo();
  if (!pushConstantsCoverRootConstantSize(Layout, VSInfo.RootConstantSize,
                                          Limits.maxPushConstantsSize,
                                          VK_SHADER_STAGE_VERTEX_BIT) ||
      !pushConstantsCoverRootConstantSize(Layout, FSInfo.RootConstantSize,
                                          Limits.maxPushConstantsSize,
                                          VK_SHADER_STAGE_FRAGMENT_BIT))
    return createStringError(
        inconvertibleErrorCode(),
        "a stage's root-constant span is not fully covered by a "
        "VkPushConstantRange visible to it in its VkPipelineLayout");
  if (Error E = validateBoundRanges(VSInfo, Layout))
    return std::move(E);
  if (Error E = validateBoundRanges(FSInfo, Layout))
    return std::move(E);

  if (Error E = validateStageInterfaces(**VertexStage, **FragmentStage,
                                        ColorAttachmentCount, VertexAttributes))
    return std::move(E);

  auto Artifact = std::make_shared<GraphicsPipelineArtifact>();
  Artifact->Ctx = std::move(Ctx);
  Artifact->VertexStage = std::move(*VertexStage);
  Artifact->FragmentStage = std::move(*FragmentStage);
  return Artifact;
}

Expected<GraphicsPipelineState>
compileGraphicsPipeline(const VkGraphicsPipelineCreateInfo &CreateInfo,
                        const PhysicalDeviceInfo &DeviceInfo,
                        PipelineCache *Cache) {
  if (!CreateInfo.layout)
    return createStringError(inconvertibleErrorCode(),
                             "graphics pipeline requires a VkPipelineLayout");

  GraphicsPipelineState Result;
  const VkPipelineShaderStageCreateInfo *VertexInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *FragmentInfo = nullptr;
  if (Error E = translateFixedFunctionState(CreateInfo, DeviceInfo, Result,
                                            VertexInfo, FragmentInfo))
    return std::move(E);

  const PipelineLayout &Layout = *fromHandle<PipelineLayout>(CreateInfo.layout);
  auto *VertexModule = fromHandle<ShaderModule>(VertexInfo->module);
  auto *FragmentModule = fromHandle<ShaderModule>(FragmentInfo->module);

  // A cache key needs the whole normalized pipeline description (see
  // "Pipeline Cache" in feme/docs/FeMeVulkanDesign.md): computed here, it
  // can be checked *before* paying for stage compilation, unlike a key
  // computed from the compiled result.
  std::optional<PipelineCacheKey> Key;
  if (Cache && VertexModule && FragmentModule) {
    std::vector<uint8_t> FixedFunctionState =
        serializeFixedFunctionState(Result);
    Key = computeGraphicsPipelineCacheKey(
        DeviceInfo.Properties.pipelineCacheUUID, VertexModule->words(),
        VertexInfo->pName ? VertexInfo->pName : "main", FragmentModule->words(),
        FragmentInfo->pName ? FragmentInfo->pName : "main", Layout.setLayouts(),
        Layout.pushConstantRanges(), FixedFunctionState);
  }

  std::shared_ptr<GraphicsPipelineArtifact> Artifact =
      Key ? Cache->lookupGraphics(*Key) : nullptr;
  if (!Artifact) {
    Expected<std::shared_ptr<GraphicsPipelineArtifact>> Compiled =
        compileAndValidateStages(
            *VertexInfo, *FragmentInfo, Layout, DeviceInfo.Properties.limits,
            static_cast<uint32_t>(Result.Attachments.size()),
            Result.VertexAttributes);
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

  return feme::graphics::GraphicsPipeline(
      State.Artifact->VertexStage, State.Artifact->FragmentStage,
      ResolvedTopology, ResolvedRaster, ResolvedDepth,
      feme::graphics::BlendMode::Replace, State.SampleCount, State.Attachments,
      ResolvedStencil, State.ColorBlends, State.LogicOpEnable, State.Logic,
      isDynamic(DynamicStateBlendConstants) ? Dynamic.BlendConstants
                                            : State.BlendConstants,
      State.PrimitiveRestartEnable);
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
    Expected<GraphicsPipelineState> Compiled =
        compileGraphicsPipeline(pCreateInfos[I], DeviceInfo, Cache);
    if (!Compiled) {
      logCreationFailure(Compiled.takeError(), "vkCreateGraphicsPipelines");
      Result = VK_ERROR_INITIALIZATION_FAILED;
      continue;
    }
    GraphicsPipeline *Obj = Alloc.create<GraphicsPipeline>(
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, std::move(*Compiled));
    if (!Obj) {
      Result = VK_ERROR_OUT_OF_HOST_MEMORY;
      continue;
    }
    pPipelines[I] = toHandle<VkPipeline>(static_cast<Pipeline *>(Obj));
  }
  return Result;
}

} // namespace feme::vulkan
