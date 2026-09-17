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
#include "GroupSize.h"
#include "Icd.h"
#include "Objects.h"
#include "PhysicalDeviceInfo.h"
#include "Pipeline.h"
#include "PipelineCache.h"
#include "RenderPass.h"
#include "SpecializationPatch.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Core/Signature.h"
#include "feme/Graphics/Geometry.h"
#include "feme/Graphics/Mesh.h"
#include "feme/Graphics/Patch.h"
#include "feme/Graphics/Tessellation.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Transforms/Graphics/CanonicalizeStage.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

#include <deque>
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
using feme::graphics::PolygonMode;
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
  // (roadmap H5e) The four adjacency topologies (list/strip, line/
  // triangle) hand a geometry stage each primitive's neighboring
  // vertices; only legal on a pipeline declaring one, and vice versa --
  // see `translateFixedFunctionState`'s own check, right after this is
  // called.
  case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
    return PrimitiveTopology::LineListWithAdjacency;
  case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
    return PrimitiveTopology::LineStripWithAdjacency;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
    return PrimitiveTopology::TriangleListWithAdjacency;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
    return PrimitiveTopology::TriangleStripWithAdjacency;
  default:
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

// (roadmap H7c) `VkPolygonMode` -> `feme::graphics::PolygonMode`.
// `VK_POLYGON_MODE_FILL_RECTANGLE_NV` is deliberately unrecognized, since
// `VK_NV_fill_rectangle` is never advertised (`getSupportedDeviceExtensions`,
// PhysicalDeviceInfo.cpp).
std::optional<PolygonMode> mapPolygonMode(VkPolygonMode Mode) {
  switch (Mode) {
  case VK_POLYGON_MODE_FILL:
    return PolygonMode::Fill;
  case VK_POLYGON_MODE_LINE:
    return PolygonMode::Line;
  case VK_POLYGON_MODE_POINT:
    return PolygonMode::Point;
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
  case VK_DYNAMIC_STATE_DEPTH_BIAS:
    return DynamicStateDepthBias;
  case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
    return DynamicStateDepthBounds;
  case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
    return DynamicStateDepthBiasEnable;
  case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
    return DynamicStateRasterizerDiscardEnable;
  default:
    return std::nullopt;
  }
}

/// Whether \p Format may be fetched as a vertex attribute: the subset
/// `feme::graphics`' own `decodeAttribute` (Executor.cpp) implements. Just
/// forwards to `Format.h`'s `isVertexBufferFormatSupported` (roadmap H8),
/// which is the single source of truth this same format set now also backs
/// `vkGetPhysicalDeviceFormatProperties`'s own
/// `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` advertisement with -- previously
/// this function duplicated that list on its own, with nothing keeping the
/// two in sync.
bool isSupportedVertexAttributeFormat(feme::cpu::ResourceFormat Format) {
  return isVertexBufferFormatSupported(Format);
}

/// (roadmap H6f) Validates \p StageInfo's own declared group size (its
/// `LocalSize`/`LocalSizeId`/`BuiltIn WorkgroupSize` execution mode) against
/// \p MaxSize/\p MaxInvocations, mirroring `Pipeline.cpp`'s
/// `compileComputePipeline` own check against `maxComputeWorkGroupSize`/
/// `Invocations` -- the mesh/task counterpart `GraphicsPipeline.h`'s
/// `MaxMeshWorkGroupSize`/`MaxTaskWorkGroupSize` comment explains was
/// missing until now. \p StageName names the stage in the returned error
/// ("mesh"/"task"), and \p LimitName names the offending property
/// ("maxMeshWorkGroupSize/Invocations"/"maxTaskWorkGroupSize/Invocations").
Error validateMeshOrTaskGroupSize(
    const VkPipelineShaderStageCreateInfo &StageInfo,
    llvm::ArrayRef<uint32_t> MaxSize, uint32_t MaxInvocations,
    llvm::StringRef StageName, llvm::StringRef LimitName) {
  // (roadmap H29d) `compileGraphicsStage` (called on the same \p StageInfo
  // before this validation runs) already resolved a null `module` through
  // an inline `VkShaderModuleCreateInfo` if one was chained; re-resolving
  // here (rather than threading its already-resolved pointer through) is a
  // deliberately cheap tradeoff -- an inline module's SPIR-V is small and
  // copying it twice is harmless, and keeping this function's own
  // signature independent of that resolution avoids coupling two call
  // sites that are otherwise unrelated.
  std::unique_ptr<ShaderModule> InlineModuleStorage;
  Expected<const ShaderModule *> ModuleOrErr =
      resolveShaderStageModule(StageInfo, InlineModuleStorage);
  if (!ModuleOrErr)
    return ModuleOrErr.takeError();
  const ShaderModule *Module = *ModuleOrErr;
  std::string EntryPoint = StageInfo.pName ? StageInfo.pName : "main";
  Expected<std::array<uint32_t, 3>> GroupSize =
      resolveComputeGroupSize(Module->words(), EntryPoint, {});
  if (!GroupSize)
    return GroupSize.takeError();
  uint64_t Invocations =
      uint64_t(GroupSize->at(0)) * GroupSize->at(1) * GroupSize->at(2);
  if ((*GroupSize)[0] > MaxSize[0] || (*GroupSize)[1] > MaxSize[1] ||
      (*GroupSize)[2] > MaxSize[2] || Invocations > MaxInvocations)
    return createStringError(inconvertibleErrorCode(),
                             "the %s stage's declared group size exceeds %s",
                             StageName.str().c_str(), LimitName.str().c_str());
  return Error::success();
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
///
/// \p OutGeometryState is \p OutState's geometry-stage counterpart
/// (roadmap H5e): filled in from the same entry point's `feme.geometry.*`
/// attributes (`feme::graphics::getGeometryState`) at the same point, for a
/// geometry module. At most one of \p OutState/\p OutGeometryState is ever
/// non-null for a given call -- a module is never both a tessellation and a
/// geometry stage.
///
/// \p OutMeshState is filled in the same way from a mesh module's own
/// `feme.mesh.*` attributes (`feme::graphics::getMeshState`, roadmap H6f).
/// Unlike \p OutState/\p OutGeometryState, a mesh module's entry point is
/// *not* rewritten by `feme::graphics::CanonicalizeStagePass` at all yet
/// (roadmap H6i is what will teach it to): the attributes this reads are
/// stamped directly by SPIR-V import (`ConvertSPIRVToLLVMPass`), so reading
/// them here needs no cooperation from that pass either way.
Expected<std::shared_ptr<feme::cpu::CompiledStage>> compileGraphicsStage(
    feme::Context &Ctx, const VkPipelineShaderStageCreateInfo &StageInfo,
    feme::ShaderStage Stage, const PipelineLayout &Layout,
    llvm::StringRef EntryPointOverride = {},
    std::optional<feme::graphics::TessellationState> *OutState = nullptr,
    std::optional<feme::graphics::GeometryState> *OutGeometryState = nullptr,
    std::optional<feme::graphics::MeshState> *OutMeshState = nullptr) {
  // (roadmap H74) Resolve this stage's real specialization overrides, then
  // patch them directly onto a private copy of the shader module's own
  // words before deserialization -- mirroring the compute path's own
  // `compileComputePipeline` (Pipeline.cpp) exactly, including its reason
  // for patching the raw SPIR-V rather than the deserialized MLIR module
  // (see `SpecializationPatch.h`'s file comment: `mlir::spirv::deserialize`
  // folds every spec constant to its module-declared default at
  // deserialization time, with no later plug-in point for a real
  // override). A private copy is used (never `Module->words()` itself)
  // since one `VkShaderModule` may back multiple pipelines, each with a
  // different `VkSpecializationInfo`.
  Expected<SmallVector<SpecializationOverride, 4>> Overrides =
      buildSpecializationOverrides(StageInfo.pSpecializationInfo);
  if (!Overrides)
    return Overrides.takeError();

  // (roadmap H29d) Resolves `StageInfo.module`, honoring
  // `VK_EXT_graphics_pipeline_library`'s inline shader-module creation (a
  // null `module` with a chained `VkShaderModuleCreateInfo`) the same way
  // the compute path's own `compileComputePipeline` does -- see
  // `resolveShaderStageModule`'s own comment (Pipeline.h).
  std::unique_ptr<ShaderModule> InlineModuleStorage;
  Expected<const ShaderModule *> ModuleOrErr =
      resolveShaderStageModule(StageInfo, InlineModuleStorage);
  if (!ModuleOrErr)
    return ModuleOrErr.takeError();
  const ShaderModule *Module = *ModuleOrErr;
  std::string DefaultEntryPoint = StageInfo.pName ? StageInfo.pName : "main";
  llvm::StringRef EntryPoint = EntryPointOverride.empty()
                                   ? llvm::StringRef(DefaultEntryPoint)
                                   : EntryPointOverride;

  SmallVector<uint32_t, 0> PatchedWords(Module->words());
  patchSpecializationConstants(PatchedWords, *Overrides);

  Expected<feme::Module> AsLLVMIR = importShaderModule(Ctx, PatchedWords);
  if (!AsLLVMIR)
    return AsLLVMIR.takeError();

  // (roadmap H88) A mesh or task entry point's group size may be declared
  // via `LocalSizeId` (spec-constant ids) rather than a literal `LocalSize`
  // -- `ConvertSPIRVToLLVMPass` only ever stamps `hlsl.numthreads` from the
  // latter (`EntryPointInfo::LocalSize`, populated solely from a plain
  // `spirv.ExecutionMode LocalSize`'s literal operands), and
  // `feme::spirv::createConvertSPIRVToLLVMPass`'s own `ExecutionModeIdPattern`
  // just drops `LocalSizeId` outright, deferring its resolution to this
  // Vulkan-level scanner (see GroupSize.h's file comment) -- so a
  // `LocalSizeId`-only entry point reaches the CPU target with no
  // `hlsl.numthreads` attribute at all, and every one of its group-size
  // readers (`DispatchArgsLayout.h`'s `getThreadGroupSize`, `SIMDize.cpp`)
  // silently default to a single-invocation `{1, 1, 1}` group instead of
  // the real, specialization-resolved size. The compute path's own
  // `compileComputePipeline` (Pipeline.cpp) already resolves and stamps
  // this after translation for exactly this reason; do the same here for
  // the two stages that can carry a compute-shaped group size.
  if (Stage == feme::ShaderStage::Mesh ||
      Stage == feme::ShaderStage::Amplification) {
    Expected<std::array<uint32_t, 3>> GroupSize =
        resolveComputeGroupSize(Module->words(), EntryPoint, *Overrides);
    if (!GroupSize)
      return GroupSize.takeError();
    if (llvm::Function *Entry =
            AsLLVMIR->getLLVMModule().getFunction(EntryPoint)) {
      std::string NumThreads =
          (llvm::Twine(GroupSize->at(0)) + "," + llvm::Twine(GroupSize->at(1)) +
           "," + llvm::Twine(GroupSize->at(2)))
              .str();
      Entry->addFnAttr("hlsl.numthreads", NumThreads);
    }
  }

  // (roadmap L12c) Resolve any unbounded (`RuntimeDescriptorArray`) resource
  // range against this pipeline's own layout before compiling -- see that
  // function's comment (Pipeline.h/.cpp; shared with the compute path).
  patchUnboundedResourceRanges(AsLLVMIR->getLLVMModule(), Layout);

  ModuleAnalysisManager MAM;
  feme::graphics::CanonicalizeStagePass().run(AsLLVMIR->getLLVMModule(), MAM);

  if (OutState || OutGeometryState || OutMeshState) {
    llvm::Function *Entry = AsLLVMIR->getLLVMModule().getFunction(EntryPoint);
    if (OutState)
      *OutState =
          Entry ? feme::graphics::getTessellationState(*Entry) : std::nullopt;
    if (OutGeometryState)
      *OutGeometryState =
          Entry ? feme::graphics::getGeometryState(*Entry) : std::nullopt;
    if (OutMeshState)
      *OutMeshState =
          Entry ? feme::graphics::getMeshState(*Entry) : std::nullopt;
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
/// fragment stage (roadmap H2j, only possible when \p ColorAttachments
/// is empty, matching `VUID-VkGraphicsPipelineCreateInfo-pStages-06894`'s
/// own condition): every fragment-side check below (varying linkage, per-
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
///
/// \p GeometryStage is non-`nullptr` for a pipeline with a geometry stage
/// (roadmap H5e), and takes over \p DomainStage's role as "the stage whose
/// own output is rasterized" whenever both are present: a geometry stage
/// runs after tessellation, and its own emitted vertices -- not the domain
/// stage's per-domain-point ones -- are what `Executor::executeDraws`
/// (roadmap H5d) actually clips/interpolates/rasterizes.
/// \p RasterizerDiscardEnable (roadmap H101b) is the pipeline's own
/// `rasterizerDiscardEnable` (`translateRasterState`'s `RasterState::
/// DiscardEnable`): when `true`, nothing this pipeline draws ever reaches
/// the rasterizer, clipper, or viewport transform at all -- there is no
/// real position for any stage to compute -- so the `SV_Position`
/// requirement below is skipped entirely, the same way \p FragmentStage
/// being `nullptr` already skips every fragment-side check further down.
/// A pure transform-feedback-capture pipeline (e.g.
/// `dEQP-VK.transform_feedback.fuzz.random_geometry.*`'s own geometry-only
/// shape: a geometry entry that only writes a captured varying block, no
/// `gl_Position`, paired with no fragment shader at all) sets exactly this
/// combination, confirmed via a real `deqp-vk` run with
/// `FEME_VULKAN_LOG_CREATION_ERRORS=1` against
/// `all_instance_array.75` (the CTS's own `makeGraphicsPipeline` helper
/// sets `rasterizerDiscardEnable = (fragmentShaderModule == VK_NULL_
/// HANDLE)`, so this pipeline's own creation info already asked for
/// exactly this).
Error validateStageInterfaces(const feme::cpu::CompiledStage &VertexStage,
                              const feme::cpu::CompiledStage *FragmentStage,
                              const feme::cpu::CompiledStage *DomainStage,
                              const feme::cpu::CompiledStage *GeometryStage,
                              llvm::ArrayRef<AttachmentFormat> ColorAttachments,
                              llvm::ArrayRef<VertexInputAttribute> Attributes,
                              bool RasterizerDiscardEnable) {
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
  std::optional<feme::EntrySignature> GeomSig;
  if (GeometryStage) {
    Expected<feme::EntrySignature> Parsed = getStageSignature(*GeometryStage);
    if (!Parsed)
      return Parsed.takeError();
    GeomSig = std::move(*Parsed);
  }
  // The stage whose own output actually reaches clipping, the viewport
  // transform and the interpolator, in precedence order: a bound geometry
  // stage's emitted vertices, else a bound domain stage's evaluated ones,
  // else the vertex stage's own -- exactly the chain `Executor::
  // executeDraws`'s `PreGeometrySig`/`RasterSig` selection already applies
  // at draw time.
  const feme::EntrySignature &PositionSig =
      GeomSig ? *GeomSig : (DomainSig ? *DomainSig : *VSSig);

  const feme::SignatureElement *Position =
      findSystemValue(PositionSig, feme::SignatureDirection::Output,
                      feme::SignatureSystemValue::Position);
  // (roadmap H5e-b) A geometry entry point that emits no vertices at all
  // (e.g. `dEQP-VK.geometry.emit.*_emit_0_end_0`'s degenerate `void
  // main(void) {}` bodies, which call neither `EmitVertex` nor
  // `EndPrimitive`) has no output signature to speak of: SPIR-V only lists
  // an entry point's *used* interface variables, so an unwritten
  // `gl_Position` simply never appears at all -- `PositionSig.Elements` is
  // empty, not just missing `Position`. Nothing is ever rasterized from
  // such a stage regardless of whether it wrote a position, so this is
  // legal, unlike a geometry stage that writes some other output (a
  // varying) but genuinely forgets `gl_Position`, which is still rejected
  // below.
  bool GeometryNeverWrites = GeometryStage && PositionSig.Elements.empty();
  if (!RasterizerDiscardEnable && !GeometryNeverWrites &&
      (!Position || Position->ComponentCount != 4))
    return createStringError(
        inconvertibleErrorCode(),
        "%s stage does not write a 4-component "
        "SV_Position output",
        GeometryStage ? "geometry"
                      : (DomainStage ? "tessellation evaluation" : "vertex"));

  if (FragmentStage) {
    Expected<feme::EntrySignature> FSSig = getStageSignature(*FragmentStage);
    if (!FSSig)
      return FSSig.takeError();

    // (roadmap H5e-b) Mirrors the `GeometryNeverWrites` relaxation above:
    // a geometry stage that emits no vertices at all never reaches the
    // fragment stage in the first place, so its own empty output
    // signature has nothing sensible to link a fragment input against
    // either. Skipping the whole location-linkage loop (rather than just
    // the lookup) also skips the fragment input's own "has no location"
    // check, which is fine -- that check exists to make the lookup below
    // meaningful, and there is no lookup to make meaningful here.
    if (!GeometryNeverWrites) {
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
            PositionSig, feme::SignatureDirection::Output, *FSIn.Location);
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
    }

    for (uint32_t I = 0; I != ColorAttachments.size(); ++I) {
      // (roadmap H7s) A `VK_ATTACHMENT_UNUSED` color-attachment slot
      // (`GraphicsPipeline.cpp`'s own `getRenderTargets` placeholder,
      // `ResourceFormat::Unknown`) still counts toward
      // `colorAttachmentCount()`/this location range, but has no real
      // attachment for a fragment output at that location to write to --
      // the write is discarded either way (`Executor.cpp`'s own
      // `Att.Data.empty()` handling), so the fragment stage is not
      // required to declare an output there at all.
      if (ColorAttachments[I].Format == feme::cpu::ResourceFormat::Unknown)
        continue;
      const feme::SignatureElement *Color =
          findLocation(*FSSig, feme::SignatureDirection::Output, I);
      // (roadmap H11) A fragment stage is not required to declare an
      // output at every location a real (non-`Unknown`) color attachment
      // occupies -- unlike a *used* attachment's own component-count/type
      // shape (still validated below when an output does exist), simply
      // never writing a location at all is legal per the Vulkan spec's own
      // fragment-output-interface rules (the color attachment keeps
      // whatever value it already held, "undefined" only in the sense
      // that this ICD need not compute one, not a validation error): CTS's
      // own `dEQP-VK.renderpasses.*.unused_clear_attachments.*` family
      // (and, transitively, every `sampleread`/`load_store_op_none`/
      // `local_read` case reusing the same pipeline shape) creates one
      // shared pipeline whose `VkPipelineRenderingCreateInfo` always names
      // every slot's real format, then renders it against a *subset* of
      // real attachments at a time (the rest `VK_NULL_HANDLE` in that
      // draw's own `VkRenderingAttachmentInfo`, `Executor.cpp`'s own
      // `Att.Data.empty()` handling) with a fragment stage that only ever
      // declares an output for the locations it plans to use across any
      // draw -- there is no requirement (and no way, at pipeline-creation
      // time) for every one of that pipeline's declared attachments to be
      // bound on every draw that reuses it. Only a *present* output's own
      // shape is still validated (below), matching what `Executor.cpp`'s
      // parallel draw-time linkage already does when an attachment is
      // genuinely bound (non-`Unknown`, non-`VK_NULL_HANDLE`) but the
      // fragment stage still has no matching output there.
      if (!Color)
        continue;
      // (roadmap H7t) A fragment output narrower than 4 components (e.g.
      // a `vec3`) is legal per the SPIR-V/GLSL spec -- the missing
      // trailing components are simply never written by the shader, and
      // `Executor.cpp`'s own `readFragmentColor` fills them with their
      // defined identity value (`0.0` for a missing green/blue channel,
      // `1.0` for a missing alpha) at draw time, mirroring
      // `ImageFixture.cpp`'s `unpackColor`'s own precedent for a color
      // format lacking a channel entirely.
      // (roadmap H29l) An integer color attachment expects a matching
      // `UInt`/`SInt` fragment output rather than a `Float` one --
      // `expectedColorComponentType` (Pipeline.h) resolves which, exactly
      // as `Executor.cpp`'s own draw-time linkage has since roadmap H8p.
      // This check used to hard-code `Float`, so it rejected at creation
      // the very `uvec4`-into-`R8G8B8A8_UINT` shape the executor was
      // already prepared to draw.
      feme::SignatureComponentType Want =
          feme::graphics::expectedColorComponentType(
              ColorAttachments[I].Format);
      if (Color->ComponentCount == 0 || Color->ComponentCount > 4 ||
          !feme::graphics::isCompatibleColorComponentType(Want,
                                                          Color->ComponentType))
        return createStringError(inconvertibleErrorCode(),
                                 "fragment stage has no %s "
                                 "output of 1-4 components at location %u "
                                 "(SV_Target%u)",
                                 Want == feme::SignatureComponentType::Float
                                     ? "floating-point"
                                     : "integer",
                                 I, I);
    }
  }
  // (roadmap H9a) A fragment-less pipeline may legally declare a nonempty
  // `ColorAttachments` too -- this used to be an invariant this function
  // itself enforced (`GraphicsPipeline.cpp`'s own pipeline-creation-time
  // rejection, removed by this same row), but is no longer one now that
  // the rejection is gone. Nothing above this point needed
  // `ColorAttachments` to be empty in the `!FragmentStage` case anyway:
  // the whole per-location fragment-output-matching loop above is already
  // scoped to `if (FragmentStage)`, so it is simply never reached here,
  // leaving every color attachment unvalidated (and, at draw time,
  // unwritten -- `Executor.cpp`'s own `!Pipeline.hasFragmentStage()`
  // early return).

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
    // (roadmap H7s) Whether any slot in `Subpass.ColorAttachments` supplied
    // a real sample count below -- tracked explicitly rather than
    // inferring it from `Subpass.ColorAttachments.empty()` the way H7n's
    // own depth/stencil-only fallback below still does, since a subpass
    // may have a non-empty color attachment list where every slot is
    // `VK_ATTACHMENT_UNUSED` (this row's own motivating shape has one real
    // slot and one unused one, but nothing rules out all-unused either).
    bool AnyColorSampleCount = false;
    for (uint32_t Index : Subpass.ColorAttachments) {
      if (Index == VK_ATTACHMENT_UNUSED) {
        // (roadmap H7s) A present-but-unused color attachment slot
        // (`VK_ATTACHMENT_UNUSED`, e.g. `dEQP-VK.pipeline.monolithic.
        // multisample.alpha_to_coverage_unused_attachment.*`'s own
        // location-0 slot): this mirrors a `VK_NULL_HANDLE`
        // `VkRenderingAttachmentInfo::imageView` (roadmap E5) -- the slot
        // still counts toward `colorAttachmentCount()`/`ColorBlends` (so
        // `Info->attachmentCount` and the fragment stage's own output
        // locations still line up one-to-one with real attachment
        // indices), but carries no format of its own and never
        // contributes a sample count. `ResourceFormat::Unknown` is never
        // consulted for an unused slot downstream (`buildRenderTargetBinding`,
        // CommandBuffer.cpp, resolves it to an empty `AttachmentView`
        // that every write already skips, the same "not bound" handling
        // an unused dynamic-rendering slot gets), so any placeholder
        // value here is safe.
        Targets.Colors.push_back(feme::cpu::ResourceFormat::Unknown);
        continue;
      }
      Targets.Colors.push_back(Pass.attachments()[Index].Format);
      Targets.SampleCount = Pass.attachments()[Index].SampleCount;
      AnyColorSampleCount = true;
    }
    if (Subpass.DepthStencilAttachment != VK_ATTACHMENT_UNUSED) {
      Targets.DepthStencil =
          Pass.attachments()[Subpass.DepthStencilAttachment].Format;
      // (roadmap H7n) A subpass with no color attachments at all (a real
      // depth/stencil-only render, e.g. `dEQP-VK.pipeline.monolithic.
      // multisample.alpha_to_coverage_no_color_attachment.*`'s own
      // `RENDER_TYPE_DEPTHSTENCIL_ONLY`) left `Targets.SampleCount` at its
      // single-sample default above -- the `for` loop over
      // `Subpass.ColorAttachments` above, the only place that ever set it,
      // never executes when that list is empty -- silently rejecting
      // every genuinely multisampled depth/stencil-only pipeline as
      // "disagreeing" with a render target whose real sample count this
      // code never actually consulted. Fall back to the depth/stencil
      // attachment's own sample count whenever no color attachment
      // supplied one -- extended by roadmap H7s to also cover a subpass
      // whose color attachment list is non-empty but every slot in it is
      // unused (the same "no color attachment actually set it" gap, just
      // reached a different way).
      if (!AnyColorSampleCount)
        Targets.SampleCount =
            Pass.attachments()[Subpass.DepthStencilAttachment].SampleCount;
    }
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
    Targets.SampleCount = static_cast<uint32_t>(
        CreateInfo.pMultisampleState->rasterizationSamples);
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
  // (roadmap H35/H74) `rasterizerDiscardEnable` needs no feature bit (core
  // 1.0 functionality); `Executor.cpp`'s shared `RasterizePrimitives` entry
  // point consults `RasterState::DiscardEnable` to skip rasterization and
  // everything downstream of it while still running every pre-
  // rasterization stage in full -- see that field's own comment.
  if ((Out.DynamicStates & DynamicStateRasterizerDiscardEnable) == 0)
    Out.Raster.DiscardEnable = Info->rasterizerDiscardEnable != VK_FALSE;
  // (roadmap H7d) `depthClamp`: a pipeline may declare `depthClampEnable`
  // regardless of whether this ICD's own `depthClamp` feature bit is
  // `VK_TRUE` (see PhysicalDeviceInfo.cpp) -- consistent with this
  // project's long-standing precedent of never gating an optional
  // feature's *translation* on whether the app actually enabled the
  // corresponding `VkPhysicalDeviceFeatures` bit. See `clipTriangle`'s and
  // `projectVertex`'s own comments in Executor.cpp for the implementation.
  Out.Raster.DepthClampEnable = Info->depthClampEnable != VK_FALSE;
  // (roadmap H7d) `depthBiasClamp`: `depthBiasEnable` itself needs no
  // feature bit (core-1.0 functionality); only a nonzero `depthBiasClamp`
  // is gated by the `depthBiasClamp` feature, and this ICD accepts that
  // unconditionally too, for the same reason as `DepthClampEnable` above.
  if ((Out.DynamicStates & DynamicStateDepthBiasEnable) == 0)
    Out.Raster.DepthBiasEnable = Info->depthBiasEnable != VK_FALSE;
  if (Info->depthBiasEnable &&
      (Out.DynamicStates & DynamicStateDepthBias) == 0) {
    Out.Raster.DepthBiasConstantFactor = Info->depthBiasConstantFactor;
    Out.Raster.DepthBiasClamp = Info->depthBiasClamp;
    Out.Raster.DepthBiasSlopeFactor = Info->depthBiasSlopeFactor;
  }
  // (roadmap H7c) `fillModeNonSolid`: `VK_POLYGON_MODE_LINE`/`_POINT`
  // rasterize a triangle-class primitive's own edges/vertices instead of
  // its filled interior -- see `PolygonMode`'s own comment
  // (feme/include/feme/Graphics/Pipeline.h) and the executor's
  // `RasterizePrimitives`. `VK_POLYGON_MODE_FILL_RECTANGLE_NV` is the only
  // real `VkPolygonMode` value this rejects (`mapPolygonMode` above).
  std::optional<PolygonMode> Polygon = mapPolygonMode(Info->polygonMode);
  if (!Polygon)
    return createStringError(inconvertibleErrorCode(),
                             "unrecognized VkPolygonMode value %u",
                             unsigned(Info->polygonMode));
  Out.Raster.Polygon = *Polygon;
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
  //
  // (roadmap H21b) `VkPipelineRasterizationStateStreamCreateInfoEXT`
  // (`VK_EXT_transform_feedback`) is recognized in the same chain: absent
  // entirely, `RasterState::RasterizationStream` keeps its own default (0,
  // stream 0 -- the only stream that exists outside a geometry shader).
  // Both structs may appear in the same `pNext` chain, so this loop no
  // longer stops at the first match the way it did when only one struct
  // type was recognized here.
  for (const VkBaseInStructure *Next =
           reinterpret_cast<const VkBaseInStructure *>(Info->pNext);
       Next; Next = Next->pNext) {
    if (Next->sType ==
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT) {
      const auto *StreamState = reinterpret_cast<
          const VkPipelineRasterizationStateStreamCreateInfoEXT *>(Next);
      Out.Raster.RasterizationStream = StreamState->rasterizationStream;
      continue;
    }
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
  // pipeline. (roadmap H7d) `DepthBoundsTestEnable` gets the same
  // treatment for the same reason: `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_
  // ENABLE`'s own resolved value always comes from `DynamicGraphicsState::
  // DepthBoundsTestEnable` instead.
  bool TestDynamic = (Out.DynamicStates & DynamicStateDepthTestEnable) != 0;
  bool WriteDynamic = (Out.DynamicStates & DynamicStateDepthWriteEnable) != 0;
  bool CompareDynamic = (Out.DynamicStates & DynamicStateDepthCompareOp) != 0;
  bool BoundsDynamic =
      (Out.DynamicStates & DynamicStateDepthBoundsTestEnable) != 0;
  bool StencilTestDynamic =
      (Out.DynamicStates & DynamicStateStencilTestEnable) != 0;
  bool StencilOpDynamic = (Out.DynamicStates & DynamicStateStencilOp) != 0;

  bool NeedsDepth = TestDynamic || WriteDynamic || BoundsDynamic;
  bool NeedsStencil = StencilTestDynamic;
  if (!Info) {
    // (roadmap H29j) Per the Vulkan spec ("If there is no depth attachment
    // then the depth test is skipped."/analogous stencil text), enabling
    // depth/stencil test (whether statically or, as here, only via a
    // dynamic state) against a render target lacking the corresponding
    // attachment is *not* a pipeline-creation-time error -- the test is
    // simply a no-op at draw time. There is nothing to resolve here (the
    // dynamic bits' actual values come from `DynamicGraphicsState` at draw
    // time, not from this function), so just accept the pipeline.
    return Error::success();
  }

  bool HasDepthAttachment =
      Targets.DepthStencil &&
      isSupportedDepthAttachmentFormat(*Targets.DepthStencil);
  NeedsDepth = NeedsDepth || Info->depthTestEnable || Info->depthWriteEnable ||
               Info->depthBoundsTestEnable;
  // (roadmap H29j) As above: a *statically* enabled depth test/write/
  // bounds-test against a render target with no (supported) depth
  // attachment is likewise just a no-op, not a rejection -- force it off
  // here instead of erroring, so the rest of this function (and the
  // draw-time state resolved from it) behaves as if depth were disabled.
  if (NeedsDepth && !HasDepthAttachment)
    NeedsDepth = false;
  if (NeedsDepth) {
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
    // (roadmap H7d) `depthBounds`: like `depthClampEnable`/`depthBias
    // Enable` (`translateRasterState`), accepted regardless of whether
    // this ICD's own `depthBounds` feature bit is `VK_TRUE`
    // (PhysicalDeviceInfo.cpp). `BoundsDynamic` (the enable bit,
    // `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE`) and `BoundsRangeDynamic`
    // (the `min`/`maxDepthBounds` values, `VK_DYNAMIC_STATE_DEPTH_BOUNDS`)
    // are independently-dynamic states, exactly like `StencilOpDynamic`
    // and the stencil reference/compare/write masks below.
    bool BoundsRangeDynamic =
        (Out.DynamicStates & DynamicStateDepthBounds) != 0;
    if (BoundsDynamic) {
      // Ignored per the comment above; resolved from
      // `DynamicGraphicsState::DepthBoundsTestEnable` instead (see
      // `buildExecutorPipeline`'s `ResolvedDepth`).
      Out.Depth.BoundsTestEnable = false;
    } else {
      Out.Depth.BoundsTestEnable = Info->depthBoundsTestEnable != VK_FALSE;
    }
    if (!BoundsRangeDynamic) {
      Out.Depth.MinDepthBounds = Info->minDepthBounds;
      Out.Depth.MaxDepthBounds = Info->maxDepthBounds;
    }
  }

  NeedsStencil = NeedsStencil || Info->stencilTestEnable;
  if (!NeedsStencil)
    return Error::success();
  // (roadmap H29j) As with depth above: statically enabling stencil test
  // against a render target with no (supported) stencil attachment is a
  // no-op, not a pipeline-creation error -- leave `Out.Stencil` disabled
  // (its zero-initialized default) instead of rejecting the pipeline.
  if (!Targets.DepthStencil ||
      !isSupportedStencilAttachmentFormat(*Targets.DepthStencil))
    return Error::success();
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
    Out.Stencil.Back.CompareMask = static_cast<uint8_t>(Info->back.compareMask);
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
/// fragment stage (roadmap H2j, no longer tied to \p Targets having no
/// color attachments as of roadmap H9a -- a fragment-less pipeline may now
/// legally have color attachments too): per the Vulkan spec, a pipeline
/// with no fragment shader has no fragment output interface, so
/// `pColorBlendState` -- including its own `attachmentCount` -- is
/// entirely ignored rather than validated against `Targets.Colors`, exactly
/// like `Info` being null below.
///
/// (roadmap H9a) Separately, `VUID-VkGraphicsPipelineCreateInfo-renderPass-
/// 07609` only requires `pColorBlendState->attachmentCount` to match
/// `Targets.Colors.size()` when "the subpass uses color attachments" --
/// when it has none at all (a depth-only render target, `Targets.Colors`
/// empty), the whole `attachmentCount` check, along with everything else
/// `pColorBlendState` configures, is simply never consulted, regardless of
/// whether a fragment stage is present. This is `GeometryShaderTestInstance`
/// /`TessellationShaderTestInstance`/`TessellationGeometryShaderTestInstance`
/// 's own shape in `dEQP-VK.query_pool.statistics_query.*`'s own
/// `noColorAttachments` variants: unlike `VertexShaderTestInstance`'s own
/// `vertexOnlyPipe` (which omits the fragment stage entirely), these keep a
/// real fragment stage but still hardcode `ColorBlendState(1,
/// &attachmentState)` unconditionally, even when the render target itself
/// has zero color attachments.
Error translateColorBlendState(const VkPipelineColorBlendStateCreateInfo *Info,
                               const PipelineRenderTargets &Targets,
                               bool HasFragmentStage,
                               GraphicsPipelineState &Out) {
  Out.ColorBlends.assign(Targets.Colors.size(), BlendState{});
  if (!Info || !HasFragmentStage || Targets.Colors.empty())
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
  appendScalar(Out, State.SampleShadingEnable);
  appendScalar(Out, State.AlphaToOneEnable);
  appendScalar(Out, State.AlphaToCoverageEnable);
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
    const VkPipelineShaderStageCreateInfo *&TessEvalInfo,
    const VkPipelineShaderStageCreateInfo *&GeometryInfo,
    const VkPipelineShaderStageCreateInfo *&MeshInfo,
    const VkPipelineShaderStageCreateInfo *&TaskInfo) {
  VertexInfo = nullptr;
  FragmentInfo = nullptr;
  TessControlInfo = nullptr;
  TessEvalInfo = nullptr;
  GeometryInfo = nullptr;
  MeshInfo = nullptr;
  TaskInfo = nullptr;
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
    case VK_SHADER_STAGE_GEOMETRY_BIT:
      // (roadmap H5e) `compileAndValidateStages` compiles this module
      // once, tagged `feme::ShaderStage::Geometry`, and merges its
      // reflected `feme::graphics::GeometryState` into `Result.Geometry`,
      // exactly the way the two tessellation stages above merge theirs
      // into `Result.Tessellation`.
      GeometryInfo = &Stage;
      break;
    case VK_SHADER_STAGE_MESH_BIT_EXT:
      // (roadmap H6f) `compileAndValidateStages` compiles this module
      // once, tagged `feme::ShaderStage::Mesh`, and merges its reflected
      // `feme::graphics::MeshState` into `Result.Mesh`, mirroring how the
      // geometry stage's own reflection is merged into `Result.Geometry`
      // above.
      MeshInfo = &Stage;
      break;
    case VK_SHADER_STAGE_TASK_BIT_EXT:
      // (roadmap H6f) Compiled tagged `feme::ShaderStage::Amplification`;
      // only legal alongside a mesh stage (checked below).
      TaskInfo = &Stage;
      break;
    default:
      return createStringError(inconvertibleErrorCode(),
                               "only the vertex, fragment, tessellation, "
                               "geometry, mesh and task stages are "
                               "implemented (V6/H4b/H5e/H6f)");
    }
  }
  // (roadmap H6f) A mesh pipeline has no vertex stage at all -- mesh and
  // vertex input are mutually exclusive ways to originate a pipeline's
  // vertices (`VUID-VkGraphicsPipelineCreateInfo-stage-02096`/neighbors).
  if (MeshInfo && VertexInfo)
    return createStringError(
        inconvertibleErrorCode(),
        "a graphics pipeline may not declare both a mesh stage and a "
        "vertex stage");
  if (!VertexInfo && !MeshInfo)
    return createStringError(
        inconvertibleErrorCode(),
        "a graphics pipeline needs a vertex stage or a mesh stage");
  // (roadmap H6f) A mesh pipeline has no input-assembly stage for
  // tessellation/geometry to attach to either -- both are entirely
  // vertex-pipeline concepts.
  if (MeshInfo && (TessControlInfo || TessEvalInfo || GeometryInfo))
    return createStringError(
        inconvertibleErrorCode(),
        "a mesh pipeline may not declare tessellation or geometry stages");
  // (roadmap H6f) The task stage only ever drives a mesh stage's dispatch
  // (`EmitMeshTasksEXT`); it is meaningless without one.
  if (TaskInfo && !MeshInfo)
    return createStringError(
        inconvertibleErrorCode(),
        "a graphics pipeline may not declare a task stage without a mesh "
        "stage");
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
  // resolve, so `Result.FragmentRobustness` is left at its default. A mesh
  // pipeline (roadmap H6f) has no vertex stage either, so
  // `Result.VertexRobustness` is likewise left at its default in that
  // case.
  if (VertexInfo) {
    Expected<PipelineRobustness> VertexRobustness =
        resolvePipelineRobustness(CreateInfo.pNext, VertexInfo->pNext);
    if (!VertexRobustness)
      return VertexRobustness.takeError();
    Result.VertexRobustness = *VertexRobustness;
  }
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
  // (roadmap H2j, loosened by H9a) A fragment shader is always optional,
  // regardless of how many color attachments the render target has: per
  // the spec's own "Valid Combinations of Stages for Graphics Pipelines"
  // text, "If a fragment shader is omitted, fragment color outputs have
  // undefined values" -- there is no VUID conditioning that on an empty
  // attachment list. This was originally rejected outright
  // (`VK_ERROR_INITIALIZATION_FAILED`), which was stricter than the spec
  // allows and broke every otherwise-valid `vertexOnlyPipe`-shaped
  // pipeline that still declares a (necessarily unwritten) color
  // attachment, such as the `dEQP-VK.query_pool.statistics_query.*`
  // suite's own `VertexShaderTestInstance` shape -- fixed by simply not
  // requiring a fragment stage here at all. `Executor.cpp`'s
  // `!Pipeline.hasFragmentStage()` early-return (roadmap H2j) already
  // leaves every color attachment untouched in this case, which is a
  // valid realization of "undefined values" (the spec permits any
  // value, including whatever was already there).

  const VkPipelineInputAssemblyStateCreateInfo *InputAssembly =
      CreateInfo.pInputAssemblyState;
  // (roadmap H6g-b) A mesh pipeline originates its own vertices/primitives
  // entirely from the mesh stage's own emitted output -- it has neither a
  // vertex-input stage nor a fixed input-assembly topology to configure.
  // Per spec, `pVertexInputState`/`pInputAssemblyState` are simply
  // *ignored* if the pipeline includes a mesh shader stage (see the
  // `VK_EXT_mesh_shader`/`VK_NV_mesh_shader` carve-out on both members'
  // own doc comments) -- unlike a vertex-stage pipeline, neither is
  // required to be null, and a real caller may well pass a non-null,
  // otherwise-default one anyway (as the common `vkObjUtil.cpp`
  // `makeGraphicsPipeline` helper `dEQP-VK.mesh_shader.*` itself builds on
  // does, unconditionally, with no mesh-aware carve-out of its own).
  // Originally this rejected any non-null pointer outright
  // (`VK_ERROR_INITIALIZATION_FAILED`), which was stricter than the spec
  // allows and broke every one of those otherwise-valid pipelines -- fixed
  // by simply not reading either pointer below, exactly as the spec's own
  // "ignored" wording requires. None of the topology/tessellation/
  // adjacency checks below apply; `Result.Topology`/
  // `Result.PrimitiveRestartEnable` are left at their defaults, unused
  // (`Executor.cpp`'s mesh path drives entirely off `MeshState::
  // OutputTopology` instead -- see `Pipeline.h`'s `hasMeshStages`).
  if (MeshInfo) {
    Result.SampleCount = Targets->SampleCount;
  } else {
    if (!InputAssembly)
      return createStringError(
          inconvertibleErrorCode(),
          "a graphics pipeline needs input assembly state");
    std::optional<PrimitiveTopology> Topology =
        mapTopology(InputAssembly->topology);
    if (!Topology)
      return createStringError(inconvertibleErrorCode(),
                               "primitive topology %u is not implemented",
                               unsigned(InputAssembly->topology));
    // (roadmap H5e-b) `Executor.cpp`'s `executeDraws` only honors
    // `primitiveRestartEnable` for the strip/fan topologies
    // `topologySupportsPrimitiveRestart` lists (every list topology has no
    // notion of restarting an assembly in progress in the first place --
    // `VUID-VkPipelineInputAssemblyStateCreateInfo-topology-00428`/
    // neighbors, since this ICD does not implement
    // `VK_EXT_primitive_topology_list_restart`); mirrored here so an
    // unsupported combination fails at creation, not silently at draw time.
    if (InputAssembly->primitiveRestartEnable &&
        !feme::graphics::topologySupportsPrimitiveRestart(*Topology))
      return createStringError(
          inconvertibleErrorCode(),
          "primitiveRestartEnable requires a strip or fan primitive "
          "topology");
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
    // (roadmap H7l) The four adjacency topologies' own adjacency vertices
    // are only ever *visible* to a geometry stage -- but per
    // `VUID-VkGraphicsPipelineCreateInfo-topology-00738`/neighbors, what
    // that requires is the `geometryShader` *device feature* being
    // enabled (`PhysicalDeviceInfo.cpp` always advertises it, unconditio-
    // nally, since H5e), not that *this* pipeline itself binds a geometry
    // stage. H5e's own check here was stricter than the spec requires --
    // found via a real `dEQP-VK.clipping.clip_volume.depth_clamp.
    // {triangle,line}_*_with_adjacency` reproduction, whose own vertex/
    // fragment-only pipelines are exactly this legal, geometry-stage-free
    // combination. A pipeline with no geometry stage simply never sees
    // the adjacency vertices at all (`Executor.cpp`'s own primitive
    // assembly now rasterizes such a pipeline's `stripAdjacency(Topology)`
    // core vertices only, per the spec's own "Primitive Topologies" text:
    // "if there is no geometry shader, ... adjacency ... is ignored").

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
  }

  const VkPhysicalDeviceLimits &Limits = DeviceInfo.Properties.limits;
  // Dynamic state is translated first: `translateDepthStencilState` below
  // needs to know whether `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE` was
  // declared before it can decide whether the static
  // `depthBoundsTestEnable` field is meaningful (see that function's own
  // comment).
  if (Error E = translateDynamicState(CreateInfo.pDynamicState, Result))
    return E;
  // (roadmap H6f) A mesh pipeline has no vertex-input state to translate
  // (checked above); `translateVertexInput` is skipped entirely rather
  // than called with a null `pVertexInputState`, which would instead
  // translate to "zero bindings/attributes" for a vertex pipeline.
  if (!MeshInfo)
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
    // (roadmap H7n) `alphaToCoverageEnable` has no `VkPhysicalDeviceFeatures`
    // gate of its own in the spec -- unlike `alphaToOneEnable`, which is
    // gated by its own feature bit, this one is always legal to enable
    // whenever multisampling itself is -- so no rejection here; see
    // `Executor.cpp`'s own per-sample coverage-mask handling.
    Result.AlphaToCoverageEnable =
        Multisample->alphaToCoverageEnable != VK_FALSE;
    // (roadmap H7f) `sampleShadingEnable`: this ICD always shades at the
    // full sample rate when set, regardless of `minSampleShading`'s own
    // fractional value -- see `GraphicsPipeline::getSampleShadingEnable`'s
    // comment in Pipeline.h for why that's spec-conformant and why
    // `minSampleShading` itself is never stored.
    Result.SampleShadingEnable = Multisample->sampleShadingEnable != VK_FALSE;
    Result.AlphaToOneEnable = Multisample->alphaToOneEnable != VK_FALSE;
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

/// Whether \p Tessellation's own `pNext` chains a
/// `VkPipelineTessellationDomainOriginStateCreateInfo` requesting
/// `VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT` -- the only domain origin
/// `flipTessellationWindingForDomainOrigin` (below) needs to know about.
/// Absent entirely, a pipeline keeps the spec's own default,
/// `VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT`.
bool hasLowerLeftTessellationDomainOrigin(
    const VkPipelineTessellationStateCreateInfo &Tessellation) {
  for (const VkBaseInStructure *Next =
           reinterpret_cast<const VkBaseInStructure *>(Tessellation.pNext);
       Next; Next = Next->pNext) {
    if (Next->sType !=
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO)
      continue;
    const auto *DomainOrigin = reinterpret_cast<
        const VkPipelineTessellationDomainOriginStateCreateInfo *>(Next);
    return DomainOrigin->domainOrigin ==
           VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
  }
  return false;
}

/// (roadmap H4i) `feme::graphics::TessOutputPrimitive::TriangleCw`/
/// `TriangleCcw` are derived purely from the domain shader's own
/// `VertexOrderCw`/`VertexOrderCcw` execution mode
/// (`ConvertSPIRVToLLVMPass.cpp`), which says nothing about
/// `VkTessellationDomainOrigin`: the tessellator's own fixed winding
/// convention (`Tessellator.cpp`'s `appendTriangle`) is only correct
/// relative to the spec's default domain origin,
/// `VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT`. Selecting
/// `VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT` mirrors the domain's own
/// coordinate frame, which reverses every generated triangle's winding as
/// a side effect (a mirror transform always reverses 2D orientation) --
/// so a `VertexOrderCw`-declaring shader under a lower-left domain origin
/// needs the *opposite* of what the tessellator would emit for it under
/// the (assumed) upper-left one, and vice versa. This flips which of the
/// two winding senses the tessellator is told to use to compensate,
/// restoring the shader's own declared vertex order relative to
/// whichever domain origin the pipeline actually requested rather than
/// always relative to the upper-left one the tessellator itself assumes.
/// `Point`/`Line` have no winding to flip.
feme::graphics::TessOutputPrimitive flipTessellationWindingForDomainOrigin(
    feme::graphics::TessOutputPrimitive Primitive) {
  switch (Primitive) {
  case feme::graphics::TessOutputPrimitive::TriangleCw:
    return feme::graphics::TessOutputPrimitive::TriangleCcw;
  case feme::graphics::TessOutputPrimitive::TriangleCcw:
    return feme::graphics::TessOutputPrimitive::TriangleCw;
  case feme::graphics::TessOutputPrimitive::Point:
  case feme::graphics::TessOutputPrimitive::Line:
    return Primitive;
  }
  llvm_unreachable("unhandled TessOutputPrimitive");
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
///
/// \p GeometryInfo is `nullptr` for a pipeline with no geometry stage
/// (roadmap H5e), in which case the returned artifact's `GeometryStage` is
/// left `nullptr` too and \p Geometry is left untouched. Otherwise the
/// module is compiled once (`ShaderStage::Geometry`), with \p Geometry
/// filled in from its own `feme.geometry.*` reflection.
///
/// \p MeshInfo is `nullptr` for a "primitive" pipeline (vertex, optionally
/// tessellation/geometry) and non-null for a mesh pipeline (roadmap H6f) --
/// `translateFixedFunctionState` already established the two are mutually
/// exclusive with \p VertexInfo, so exactly one of them is non-null on
/// entry. When \p MeshInfo is set, the module is compiled once
/// (`ShaderStage::Mesh`), \p Mesh is filled in from its own `feme.mesh.*`
/// reflection and validated against \p MeshOutputLimits (`maxMeshOutput
/// Vertices`/`maxMeshOutputPrimitives`, mirroring how \p Geometry's own
/// `Invocations`/`MaxOutputVertices` are validated against \p Limits
/// above), and the returned artifact's `VertexStage`/`HullStage`/
/// `PatchConstantStage`/`DomainStage`/`GeometryStage` are all left
/// `nullptr`. \p TaskInfo is `nullptr` for a mesh pipeline with no task
/// stage (also legal, see `graphics::GraphicsPipeline::hasTaskStage`) and
/// otherwise compiled once too (`ShaderStage::Amplification`), with no
/// reflected state of its own to validate (`Mesh.h`'s own comment: a task
/// entry declares no shape beyond its workgroup size).
Expected<std::shared_ptr<GraphicsPipelineArtifact>> compileAndValidateStages(
    const VkPipelineShaderStageCreateInfo *VertexInfo,
    const VkPipelineShaderStageCreateInfo *FragmentInfo,
    const VkPipelineShaderStageCreateInfo *TessControlInfo,
    const VkPipelineShaderStageCreateInfo *TessEvalInfo,
    const VkPipelineShaderStageCreateInfo *GeometryInfo,
    const VkPipelineShaderStageCreateInfo *MeshInfo,
    const VkPipelineShaderStageCreateInfo *TaskInfo,
    const PipelineLayout &Layout, const VkPhysicalDeviceLimits &Limits,
    llvm::ArrayRef<AttachmentFormat> ColorAttachments,
    llvm::ArrayRef<VertexInputAttribute> VertexAttributes,
    feme::graphics::TessellationState &Tessellation,
    feme::graphics::GeometryState &Geometry, feme::graphics::MeshState &Mesh,
    bool RasterizerDiscardEnable) {
  auto Ctx = std::make_unique<feme::Context>();
  Ctx->setDiagnosticHandler([](const feme::Diagnostic &) {});

  // (roadmap H6f) A mesh pipeline has no vertex stage at all (see this
  // function's own comment above); the whole "primitive" pipeline half
  // below (vertex/tessellation/geometry) is skipped for one, and a
  // separate mesh/task-only path runs instead further down.
  std::shared_ptr<feme::cpu::CompiledStage> VertexStageCompiled;
  if (VertexInfo) {
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> Compiled =
        compileGraphicsStage(*Ctx, *VertexInfo, feme::ShaderStage::Vertex,
                             Layout);
    if (!Compiled)
      return Compiled.takeError();
    VertexStageCompiled = std::move(*Compiled);
  }
  std::shared_ptr<feme::cpu::CompiledStage> FragmentStage;
  if (FragmentInfo) {
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> Compiled =
        compileGraphicsStage(*Ctx, *FragmentInfo, feme::ShaderStage::Fragment,
                             Layout);
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
                             Layout, ControlEntry, &ControlPointState);
    if (!HullCompiled)
      return HullCompiled.takeError();
    HullStage = std::move(*HullCompiled);

    Expected<std::shared_ptr<feme::cpu::CompiledStage>> PatchConstantCompiled =
        compileGraphicsStage(*Ctx, *TessControlInfo, feme::ShaderStage::Hull,
                             Layout, PatchConstantEntry);
    if (!PatchConstantCompiled)
      return PatchConstantCompiled.takeError();
    PatchConstantStage = std::move(*PatchConstantCompiled);

    std::optional<feme::graphics::TessellationState> DomainState;
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> DomainCompiled =
        compileGraphicsStage(*Ctx, *TessEvalInfo, feme::ShaderStage::Domain,
                             Layout, {}, &DomainState);
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
    // (roadmap L77) The domain-shape execution modes (Triangles/Quads/
    // Isolines + spacing + vertex-order/point-mode) are not reliably on
    // the tessellation-evaluation entry point alone: real DXC output
    // declares the full group on the tessellation-control entry point
    // instead, only duplicating `Triangles` onto the tessellation-
    // evaluation one, so `DomainState` alone is incomplete for that real
    // shape even though its own entry point is not malformed. Prefer
    // `DomainState`'s own domain shape when it has one (the
    // Khronos-spec-implied split this code originally assumed, still
    // valid for a module that really does declare it there), falling
    // back to `ControlPointState`'s when it doesn't; only reject the
    // pipeline if neither half declares a domain shape at all.
    const feme::graphics::TessellationState *DomainShape = nullptr;
    if (DomainState && DomainState->HasDomainShape)
      DomainShape = &*DomainState;
    else if (ControlPointState->HasDomainShape)
      DomainShape = &*ControlPointState;
    if (!DomainShape)
      return createStringError(
          inconvertibleErrorCode(),
          "neither the tessellation-control stage's entry point '%s' nor "
          "the tessellation-evaluation stage declares a tessellation "
          "domain execution mode (Triangles/Quads/Isolines)",
          ControlEntry.c_str());
    Tessellation.OutputControlPointCount =
        ControlPointState->OutputControlPointCount;
    Tessellation.Domain = DomainShape->Domain;
    Tessellation.Partitioning = DomainShape->Partitioning;
    Tessellation.OutputPrimitive = DomainShape->OutputPrimitive;

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

  std::shared_ptr<feme::cpu::CompiledStage> GeometryStageCompiled;
  if (GeometryInfo) {
    std::optional<feme::graphics::GeometryState> GeomState;
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> Compiled =
        compileGraphicsStage(*Ctx, *GeometryInfo, feme::ShaderStage::Geometry,
                             Layout, {}, nullptr, &GeomState);
    if (!Compiled)
      return Compiled.takeError();
    GeometryStageCompiled = std::move(*Compiled);

    // (roadmap H5e) Mirrors the tessellation halves' own "declares no
    // execution mode" check just above: a geometry entry point that failed
    // to declare its input/output primitive class has nothing sensible to
    // run the executor's assembly/rasterization against.
    if (!GeomState)
      return createStringError(
          inconvertibleErrorCode(),
          "the geometry stage declares no input/output primitive class "
          "execution mode");
    // (roadmap H5e) Mirrors `validatePatchControlPointCounts`'s own role
    // for the tessellation halves above: `Invocations`/`MaxOutputVertices`
    // are the two geometry-stage limits with a single declared scalar to
    // check them against (`maxGeometryShaderInvocations`/
    // `maxGeometryOutputVertices`); the remaining `maxGeometry*` limits
    // are per-signature component-count sums with no counterpart
    // enforcement yet on the tessellation side either (its own
    // `maxTessellationControlPer{Vertex,Patch}*Components` limits are
    // likewise advertised, honest ceilings that are not independently
    // re-checked here).
    if (GeomState->Invocations > Limits.maxGeometryShaderInvocations)
      return createStringError(
          inconvertibleErrorCode(),
          "the geometry stage's Invocations execution mode (%u) exceeds "
          "maxGeometryShaderInvocations (%u)",
          GeomState->Invocations, Limits.maxGeometryShaderInvocations);
    if (GeomState->MaxOutputVertices > Limits.maxGeometryOutputVertices)
      return createStringError(
          inconvertibleErrorCode(),
          "the geometry stage's OutputVertices execution mode (%u) exceeds "
          "maxGeometryOutputVertices (%u)",
          GeomState->MaxOutputVertices, Limits.maxGeometryOutputVertices);
    Geometry = *GeomState;
  }

  // (roadmap H6f) `MeshInfo`/`TaskInfo` are set exactly for a mesh pipeline
  // (`translateFixedFunctionState` already established `VertexInfo`/
  // `MeshInfo` are mutually exclusive); `TaskInfo` is separately optional
  // even then (see this function's own comment above).
  std::shared_ptr<feme::cpu::CompiledStage> MeshStageCompiled;
  std::shared_ptr<feme::cpu::CompiledStage> TaskStageCompiled;
  if (MeshInfo) {
    std::optional<feme::graphics::MeshState> MeshShapeState;
    Expected<std::shared_ptr<feme::cpu::CompiledStage>> Compiled =
        compileGraphicsStage(*Ctx, *MeshInfo, feme::ShaderStage::Mesh,
                             Layout, {}, nullptr, nullptr, &MeshShapeState);
    if (!Compiled)
      return Compiled.takeError();
    MeshStageCompiled = std::move(*Compiled);

    // (roadmap H6f) Mirrors the geometry stage's own "declares no
    // execution mode" check above: a mesh entry point that failed to
    // declare its output topology/counts has nothing sensible to run the
    // executor's meshlet assembly against.
    if (!MeshShapeState)
      return createStringError(
          inconvertibleErrorCode(),
          "the mesh stage declares no output topology/count execution "
          "mode (OutputPoints/OutputLinesEXT/OutputTrianglesEXT, "
          "OutputVertices, OutputPrimitivesEXT)");
    // (roadmap H6f) The two mesh-stage limits with a single declared
    // scalar to check them against, mirroring the geometry stage's own
    // `Invocations`/`MaxOutputVertices` check above.
    if (MeshShapeState->MaxOutputVertices > MaxMeshOutputVertices)
      return createStringError(
          inconvertibleErrorCode(),
          "the mesh stage's OutputVertices execution mode (%u) exceeds "
          "maxMeshOutputVertices (%u)",
          MeshShapeState->MaxOutputVertices, MaxMeshOutputVertices);
    if (MeshShapeState->MaxOutputPrimitives > MaxMeshOutputPrimitives)
      return createStringError(
          inconvertibleErrorCode(),
          "the mesh stage's OutputPrimitivesEXT execution mode (%u) "
          "exceeds maxMeshOutputPrimitives (%u)",
          MeshShapeState->MaxOutputPrimitives, MaxMeshOutputPrimitives);
    Mesh = *MeshShapeState;

    if (Error Err = validateMeshOrTaskGroupSize(
            *MeshInfo, feme::vulkan::MaxMeshWorkGroupSize,
            feme::vulkan::MaxMeshWorkGroupInvocations, "mesh",
            "maxMeshWorkGroupSize/Invocations"))
      return std::move(Err);

    if (TaskInfo) {
      Expected<std::shared_ptr<feme::cpu::CompiledStage>> TaskCompiled =
          compileGraphicsStage(*Ctx, *TaskInfo,
                               feme::ShaderStage::Amplification, Layout);
      if (!TaskCompiled)
        return TaskCompiled.takeError();
      TaskStageCompiled = std::move(*TaskCompiled);

      if (Error Err = validateMeshOrTaskGroupSize(
              *TaskInfo, feme::vulkan::MaxTaskWorkGroupSize,
              feme::vulkan::MaxTaskWorkGroupInvocations, "task",
              "maxTaskWorkGroupSize/Invocations"))
        return std::move(Err);
    }
  }

  std::shared_ptr<feme::cpu::CompiledStage> VertexStage =
      std::move(VertexStageCompiled);
  if (VertexStage) {
    const feme::cpu::ResourceInfo &VSInfo = VertexStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, VSInfo.RootConstantSize,
                                            VSInfo.RootConstantMinOffset,
                                            Limits.maxPushConstantsSize,
                                            VK_SHADER_STAGE_VERTEX_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(VSInfo, Layout))
      return std::move(E);
  }
  if (FragmentStage) {
    const feme::cpu::ResourceInfo &FSInfo = FragmentStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, FSInfo.RootConstantSize,
                                            FSInfo.RootConstantMinOffset,
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
            Layout, HSInfo.RootConstantSize, HSInfo.RootConstantMinOffset,
            Limits.maxPushConstantsSize,
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(HSInfo, Layout))
      return std::move(E);
    const feme::cpu::ResourceInfo &DSInfo = DomainStage->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(
            Layout, DSInfo.RootConstantSize, DSInfo.RootConstantMinOffset,
            Limits.maxPushConstantsSize,
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(DSInfo, Layout))
      return std::move(E);
  }
  if (GeometryStageCompiled) {
    const feme::cpu::ResourceInfo &GSInfo =
        GeometryStageCompiled->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, GSInfo.RootConstantSize,
                                            GSInfo.RootConstantMinOffset,
                                            Limits.maxPushConstantsSize,
                                            VK_SHADER_STAGE_GEOMETRY_BIT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(GSInfo, Layout))
      return std::move(E);
  }
  if (MeshStageCompiled) {
    const feme::cpu::ResourceInfo &MSInfo =
        MeshStageCompiled->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, MSInfo.RootConstantSize,
                                            MSInfo.RootConstantMinOffset,
                                            Limits.maxPushConstantsSize,
                                            VK_SHADER_STAGE_MESH_BIT_EXT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(MSInfo, Layout))
      return std::move(E);
  }
  if (TaskStageCompiled) {
    const feme::cpu::ResourceInfo &TSInfo =
        TaskStageCompiled->getResourceInfo();
    if (!pushConstantsCoverRootConstantSize(Layout, TSInfo.RootConstantSize,
                                            TSInfo.RootConstantMinOffset,
                                            Limits.maxPushConstantsSize,
                                            VK_SHADER_STAGE_TASK_BIT_EXT))
      return createStringError(
          inconvertibleErrorCode(),
          "a stage's root-constant span is not fully covered by a "
          "VkPushConstantRange visible to it in its VkPipelineLayout");
    if (Error E = validateBoundRanges(TSInfo, Layout))
      return std::move(E);
  }

  // (roadmap H6f) A mesh pipeline's entry points have no reflected
  // `feme::EntrySignature` to validate a cross-stage interface against yet
  // (`compileGraphicsStage`'s own comment on `feme::graphics::
  // CanonicalizeStagePass` not touching mesh/task entries -- that is
  // roadmap H6i's job); `validateStageInterfaces` is entirely a "primitive"
  // pipeline concept, skipped here rather than called against a vertex
  // stage that does not exist for one.
  if (VertexStage) {
    if (Error E = validateStageInterfaces(
            *VertexStage, FragmentStage.get(), DomainStage.get(),
            GeometryStageCompiled.get(), ColorAttachments,
            VertexAttributes, RasterizerDiscardEnable))
      return std::move(E);
  }

  auto Artifact = std::make_shared<GraphicsPipelineArtifact>();
  Artifact->Ctx = std::move(Ctx);
  Artifact->VertexStage = std::move(VertexStage);
  Artifact->FragmentStage = std::move(FragmentStage);
  Artifact->HullStage = std::move(HullStage);
  Artifact->PatchConstantStage = std::move(PatchConstantStage);
  Artifact->DomainStage = std::move(DomainStage);
  Artifact->GeometryStage = std::move(GeometryStageCompiled);
  Artifact->MeshStage = std::move(MeshStageCompiled);
  Artifact->TaskStage = std::move(TaskStageCompiled);
  // (roadmap H29o) Carry the state reflected above into the artifact, so a
  // later pipeline satisfied from the cache -- which never re-runs this
  // function, and so never sees the un-JIT-ed modules reflection needs --
  // recovers it instead of silently keeping its defaults.
  Artifact->Tessellation = Tessellation;
  Artifact->Geometry = Geometry;
  Artifact->Mesh = Mesh;
  return Artifact;
}

Expected<std::optional<GraphicsPipelineState>>
compileGraphicsPipeline(const VkGraphicsPipelineCreateInfo &CreateInfo,
                        const PhysicalDeviceInfo &DeviceInfo,
                        PipelineCache *Cache, PipelineCache &ImplicitCache,
                        bool &CacheHit) {
  CacheHit = false;
  if (!CreateInfo.layout)
    return createStringError(inconvertibleErrorCode(),
                             "graphics pipeline requires a VkPipelineLayout");

  GraphicsPipelineState Result;
  const VkPipelineShaderStageCreateInfo *VertexInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *FragmentInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *TessControlInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *TessEvalInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *GeometryInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *MeshInfo = nullptr;
  const VkPipelineShaderStageCreateInfo *TaskInfo = nullptr;
  if (Error E = translateFixedFunctionState(
          CreateInfo, DeviceInfo, Result, VertexInfo, FragmentInfo,
          TessControlInfo, TessEvalInfo, GeometryInfo, MeshInfo, TaskInfo))
    return std::move(E);

  const PipelineLayout &Layout = *fromHandle<PipelineLayout>(CreateInfo.layout);
  // (roadmap H6f) `VertexInfo` is `nullptr` for a mesh pipeline; the module
  // (and the words/entry point fed into the cache key below) follows it to
  // a null/empty state the same way `FragmentModule` already does for a
  // fragment-less pipeline.
  auto *VertexModule =
      VertexInfo ? fromHandle<ShaderModule>(VertexInfo->module) : nullptr;
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
  // (roadmap H5e) `GeometryInfo` is `nullptr` for a pipeline with no
  // geometry stage; the module (and the words/entry point fed into the
  // cache key below) follows it to a null/empty state the same way.
  auto *GeometryModule =
      GeometryInfo ? fromHandle<ShaderModule>(GeometryInfo->module) : nullptr;
  // (roadmap H6f) `MeshInfo` is `nullptr` for a "primitive" pipeline;
  // `TaskInfo` is separately `nullptr` for a mesh pipeline with no task
  // stage. Both modules (and their words/entry points fed into the cache
  // key below) follow them to a null/empty state the same way.
  auto *MeshModule =
      MeshInfo ? fromHandle<ShaderModule>(MeshInfo->module) : nullptr;
  auto *TaskModule =
      TaskInfo ? fromHandle<ShaderModule>(TaskInfo->module) : nullptr;

  // A cache key needs the whole normalized pipeline description (see
  // "Pipeline Cache" in feme/docs/FeMeVulkanDesign.md): computed here, it
  // can be checked *before* paying for stage compilation, unlike a key
  // computed from the compiled result. (roadmap H29d) A stage using inline
  // shader-module creation (`VkPipelineShaderStageCreateInfo::module ==
  // VK_NULL_HANDLE`) has no `ShaderModule` handle for `fromHandle` to
  // resolve, so its `*Module` stays null and the condition below is false
  // for that stage -- caching is simply skipped for such a pipeline rather
  // than keying on the inline `VkShaderModuleCreateInfo`'s own bytes, an
  // honest (if not maximally performant) simplification.
  std::optional<PipelineCacheKey> Key;
  if ((VertexInfo ? VertexModule : MeshModule) &&
      (!FragmentInfo || FragmentModule) &&
      (!TessControlInfo || (TessControlModule && TessEvalModule)) &&
      (!GeometryInfo || GeometryModule) && (!MeshInfo || MeshModule) &&
      (!TaskInfo || TaskModule)) {
    std::vector<uint8_t> FixedFunctionState =
        serializeFixedFunctionState(Result);
    llvm::ArrayRef<uint32_t> VertexWords =
        VertexModule ? VertexModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef VertexEntry =
        VertexInfo ? (VertexInfo->pName ? VertexInfo->pName : "main")
                   : llvm::StringRef();
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
    llvm::ArrayRef<uint32_t> GeometryWords =
        GeometryModule ? GeometryModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef GeometryEntry =
        GeometryInfo ? (GeometryInfo->pName ? GeometryInfo->pName : "main")
                     : llvm::StringRef();
    llvm::ArrayRef<uint32_t> MeshWords =
        MeshModule ? MeshModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef MeshEntry =
        MeshInfo ? (MeshInfo->pName ? MeshInfo->pName : "main")
                 : llvm::StringRef();
    llvm::ArrayRef<uint32_t> TaskWords =
        TaskModule ? TaskModule->words() : llvm::ArrayRef<uint32_t>();
    llvm::StringRef TaskEntry =
        TaskInfo ? (TaskInfo->pName ? TaskInfo->pName : "main")
                 : llvm::StringRef();
    Key = computeGraphicsPipelineCacheKey(
        DeviceInfo.Properties.pipelineCacheUUID, VertexWords, VertexEntry,
        FragmentWords, FragmentEntry, Layout.setLayouts(),
        Layout.pushConstantRanges(), FixedFunctionState, TessControlWords,
        TessControlEntry, TessEvalWords, TessEvalEntry, GeometryWords,
        GeometryEntry, MeshWords, MeshEntry, TaskWords, TaskEntry);
  }

  // (roadmap L89c) The app's own cache is consulted first and is the only
  // one whose hit may be reported through
  // `VK_EXT_pipeline_creation_feedback`; the device's implicit cache backs
  // it up on every creation, including the common one that supplied no
  // cache at all. See `Device::getImplicitPipelineCache`.
  std::shared_ptr<GraphicsPipelineArtifact> Artifact =
      Key && Cache ? Cache->lookupGraphics(*Key) : nullptr;
  CacheHit = Artifact != nullptr;
  if (!Artifact && Key) {
    Artifact = ImplicitCache.lookupGraphics(*Key);
    // An implicit hit still populates the app's cache, so a later creation
    // through it reports the application-cache hit the app is entitled to
    // expect after having created one.
    if (Artifact && Cache)
      Cache->insertGraphics(*Key, Artifact);
  }
  if (!Artifact) {
    // (roadmap E9) `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_
    // BIT`: this pipeline missed the cache (or none was given), and the
    // caller asked to be told rather than pay for a real compile here.
    if (CreateInfo.flags &
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return std::nullopt;
    Expected<std::shared_ptr<GraphicsPipelineArtifact>> Compiled =
        compileAndValidateStages(
            VertexInfo, FragmentInfo, TessControlInfo, TessEvalInfo,
            GeometryInfo, MeshInfo, TaskInfo, Layout,
            DeviceInfo.Properties.limits,
            llvm::ArrayRef(Result.Attachments),
            Result.VertexAttributes, Result.Tessellation, Result.Geometry,
            Result.Mesh, Result.Raster.DiscardEnable);
    if (!Compiled)
      return Compiled.takeError();
    Artifact = std::move(*Compiled);
    if (Key) {
      ImplicitCache.insertGraphics(*Key, Artifact);
      if (Cache)
        Cache->insertGraphics(*Key, Artifact);
    }
  }
  Result.Artifact = std::move(Artifact);
  // (roadmap H29o) On a cache hit `compileAndValidateStages` never ran, so
  // `Result.Tessellation`/`Geometry`/`Mesh` are still default-constructed
  // -- a geometry stage would claim a `Points` input primitive and a zero
  // `MaxOutputVertices` regardless of what it actually declared. Take them
  // from the artifact, which carries the values reflected when these same
  // modules were really compiled. Assigning unconditionally is safe on a
  // miss too: the artifact was just built from these very variables.
  Result.Tessellation = Result.Artifact->Tessellation;
  Result.Geometry = Result.Artifact->Geometry;
  Result.Mesh = Result.Artifact->Mesh;
  // (roadmap H4i) `VkTessellationDomainOrigin` is a pipeline-level create
  // parameter, not something a compiled shader's own reflection carries
  // (`compileAndValidateStages`'s `Result.Tessellation.OutputPrimitive`
  // above says nothing about it) -- and, for a cache hit, isn't repeated
  // per compile at all -- so it is applied once here, unconditionally,
  // against whatever `OutputPrimitive` this pipeline ended up with either
  // way. `CreateInfo.pTessellationState` is non-null whenever
  // `TessControlInfo` is (`translateFixedFunctionState` already required
  // it above).
  if (TessControlInfo &&
      hasLowerLeftTessellationDomainOrigin(*CreateInfo.pTessellationState))
    Result.Tessellation.OutputPrimitive =
        flipTessellationWindingForDomainOrigin(
            Result.Tessellation.OutputPrimitive);
  return Result;
}

} // namespace

namespace feme::vulkan {

/// (roadmap H29b) Deep-copies one `VkPipelineShaderStageCreateInfo` for
/// `GraphicsPipelineLibraryState`'s own `PreRasterizationStages`/
/// `FragmentStage` -- see that struct's own comment.
static GraphicsPipelineLibraryStage
captureLibraryStage(const VkPipelineShaderStageCreateInfo &Stage) {
  GraphicsPipelineLibraryStage Out;
  Out.Stage = Stage.stage;
  Out.Module = Stage.module;
  Out.Name = Stage.pName ? Stage.pName : "main";
  if (const VkSpecializationInfo *Spec = Stage.pSpecializationInfo) {
    Out.SpecMapEntries.assign(Spec->pMapEntries,
                              Spec->pMapEntries + Spec->mapEntryCount);
    const auto *Data = static_cast<const uint8_t *>(Spec->pData);
    Out.SpecData.assign(Data, Data + Spec->dataSize);
  }
  // (roadmap H29i) A null `Module` may still carry real shader code via
  // H29d's own inline-shader-module path (a chained
  // `VkShaderModuleCreateInfo`); that struct's own `pCode` needs deep
  // copying too, exactly like everything else this function captures,
  // since `Stage.pNext` need not outlive this call either. Leaving
  // `InlineModuleWords` empty when no such struct is chained (an
  // application error either way) lets the existing "null module and no
  // chained info" diagnostic fire later at link/compile time instead of
  // duplicating it here.
  if (!Out.Module) {
    for (const auto *Header =
             static_cast<const VkBaseInStructure *>(Stage.pNext);
         Header; Header = Header->pNext) {
      if (Header->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO)
        continue;
      const auto *Info =
          reinterpret_cast<const VkShaderModuleCreateInfo *>(Header);
      if (Info->codeSize != 0 && Info->codeSize % sizeof(uint32_t) == 0) {
        const auto *Code = static_cast<const uint32_t *>(Info->pCode);
        Out.InlineModuleWords.assign(Code,
                                     Code + Info->codeSize / sizeof(uint32_t));
      }
      break;
    }
  }
  return Out;
}

/// (roadmap H29n) Folds \p Child's own captured state into \p Out for every
/// `VkGraphicsPipelineLibraryFlagBitsEXT` part \p Child provides that \p Out
/// does not already provide itself, flattening a nested pipeline-library
/// tree one level at a time. `VK_EXT_graphics_pipeline_library` lets a
/// library be built by linking *other* libraries (a library create call may
/// chain its own `VkPipelineLibraryCreateInfoKHR`), and a library whose
/// parts all come from its children legitimately declares no
/// `VkGraphicsPipelineLibraryCreateInfoEXT::flags` of its own at all.
/// Since each part may be provided exactly once across a whole link, a bit
/// \p Out already has can never also be owned by a child, so "own state
/// wins" is a total order rather than a tie-break policy.
static void foldLinkedLibraryState(GraphicsPipelineLibraryState &Out,
                                   const GraphicsPipelineLibraryState &Child) {
  if (!Out.Layout)
    Out.Layout = Child.Layout;
  if (!Out.RenderPass) {
    Out.RenderPass = Child.RenderPass;
    Out.Subpass = Child.Subpass;
  }
  for (VkDynamicState State : Child.DynamicStates)
    if (llvm::find(Out.DynamicStates, State) == Out.DynamicStates.end())
      Out.DynamicStates.push_back(State);

  const VkGraphicsPipelineLibraryFlagsEXT New = Child.Flags & ~Out.Flags;
  if (New & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
    Out.VertexBindings = Child.VertexBindings;
    Out.VertexAttributes = Child.VertexAttributes;
    Out.InputAssembly = Child.InputAssembly;
  }
  if (New & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
    Out.PreRasterizationStages = Child.PreRasterizationStages;
    Out.Viewports = Child.Viewports;
    Out.Scissors = Child.Scissors;
    Out.ViewportState = Child.ViewportState;
    Out.RasterizationState = Child.RasterizationState;
    Out.TessellationState = Child.TessellationState;
  }
  if (New & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
    Out.FragmentStage = Child.FragmentStage;
    Out.DepthStencilState = Child.DepthStencilState;
  }
  if (New & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
    Out.ColorBlendAttachments = Child.ColorBlendAttachments;
    Out.ColorBlendState = Child.ColorBlendState;
    Out.ColorAttachmentFormats = Child.ColorAttachmentFormats;
    Out.RenderingCreateInfo = Child.RenderingCreateInfo;
  }
  // Multisample state is shared by the fragment-shader/fragment-output
  // parts alike, so it is not owned by a single bit and cannot be folded
  // by the `New` mask above: take a child's whenever this level has none.
  if (!Out.MultisampleState && Child.MultisampleState) {
    Out.SampleMask = Child.SampleMask;
    Out.MultisampleState = Child.MultisampleState;
  }

  Out.Flags |= Child.Flags;
}

GraphicsPipelineLibraryState captureGraphicsPipelineLibraryState(
    const VkGraphicsPipelineCreateInfo &CreateInfo,
    VkGraphicsPipelineLibraryFlagsEXT Flags) {
  GraphicsPipelineLibraryState Out;
  Out.Flags = Flags;
  Out.Layout = CreateInfo.layout;
  Out.RenderPass = CreateInfo.renderPass;
  Out.Subpass = CreateInfo.subpass;
  if (const auto *DS = CreateInfo.pDynamicState)
    Out.DynamicStates.assign(DS->pDynamicStates,
                             DS->pDynamicStates + DS->dynamicStateCount);

  if (Flags & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
    if (const auto *VI = CreateInfo.pVertexInputState) {
      Out.VertexBindings.assign(VI->pVertexBindingDescriptions,
                                VI->pVertexBindingDescriptions +
                                    VI->vertexBindingDescriptionCount);
      Out.VertexAttributes.assign(VI->pVertexAttributeDescriptions,
                                  VI->pVertexAttributeDescriptions +
                                      VI->vertexAttributeDescriptionCount);
    }
    if (const auto *IA = CreateInfo.pInputAssemblyState) {
      VkPipelineInputAssemblyStateCreateInfo Copy = *IA;
      Copy.pNext = nullptr;
      Out.InputAssembly = Copy;
    }
  }

  if (Flags & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
    for (uint32_t I = 0; I != CreateInfo.stageCount; ++I)
      if (CreateInfo.pStages[I].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
        Out.PreRasterizationStages.push_back(
            captureLibraryStage(CreateInfo.pStages[I]));
    if (const auto *VP = CreateInfo.pViewportState) {
      VkPipelineViewportStateCreateInfo Copy = *VP;
      if (Copy.pViewports)
        Out.Viewports.assign(Copy.pViewports,
                             Copy.pViewports + Copy.viewportCount);
      if (Copy.pScissors)
        Out.Scissors.assign(Copy.pScissors, Copy.pScissors + Copy.scissorCount);
      Copy.pNext = nullptr;
      Copy.pViewports = nullptr;
      Copy.pScissors = nullptr;
      Out.ViewportState = Copy;
    }
    if (const auto *RS = CreateInfo.pRasterizationState) {
      VkPipelineRasterizationStateCreateInfo Copy = *RS;
      Copy.pNext = nullptr;
      Out.RasterizationState = Copy;
    }
    if (const auto *TS = CreateInfo.pTessellationState) {
      VkPipelineTessellationStateCreateInfo Copy = *TS;
      Copy.pNext = nullptr;
      Out.TessellationState = Copy;
    }
  }

  if (Flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
    for (uint32_t I = 0; I != CreateInfo.stageCount; ++I)
      if (CreateInfo.pStages[I].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
        Out.FragmentStage = captureLibraryStage(CreateInfo.pStages[I]);
    if (const auto *DS = CreateInfo.pDepthStencilState) {
      VkPipelineDepthStencilStateCreateInfo Copy = *DS;
      Copy.pNext = nullptr;
      Out.DepthStencilState = Copy;
    }
  }

  if (Flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
    if (const auto *CB = CreateInfo.pColorBlendState) {
      VkPipelineColorBlendStateCreateInfo Copy = *CB;
      if (Copy.pAttachments)
        Out.ColorBlendAttachments.assign(
            Copy.pAttachments, Copy.pAttachments + Copy.attachmentCount);
      Copy.pNext = nullptr;
      Copy.pAttachments = nullptr;
      Out.ColorBlendState = Copy;
    }
    // (roadmap H29m) Only this part owns the render target's attachment
    // formats, so only this part's own chained
    // `VkPipelineRenderingCreateInfo` is captured -- see
    // `GraphicsPipelineLibraryState::RenderingCreateInfo`'s own comment for
    // why reading one from any other part would be a real bug rather than
    // merely redundant.
    if (const VkPipelineRenderingCreateInfo *RI =
            findRenderingCreateInfo(CreateInfo.pNext)) {
      VkPipelineRenderingCreateInfo Copy = *RI;
      if (Copy.pColorAttachmentFormats)
        Out.ColorAttachmentFormats.assign(Copy.pColorAttachmentFormats,
                                          Copy.pColorAttachmentFormats +
                                              Copy.colorAttachmentCount);
      Copy.pNext = nullptr;
      Copy.pColorAttachmentFormats = nullptr;
      Out.RenderingCreateInfo = Copy;
    }
  }

  // (roadmap H29b) `pMultisampleState` is shared by `FRAGMENT_SHADER_BIT`/
  // `FRAGMENT_OUTPUT_INTERFACE_BIT` alike (the spec's own "Multiple
  // Pipeline Creation" table lists it under both), so it is captured
  // whenever either bit is set rather than gated on just one.
  if ((Flags &
       (VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
        VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)) &&
      CreateInfo.pMultisampleState) {
    VkPipelineMultisampleStateCreateInfo Copy = *CreateInfo.pMultisampleState;
    if (Copy.pSampleMask) {
      uint32_t MaskWords =
          (static_cast<uint32_t>(Copy.rasterizationSamples) + 31) / 32;
      Out.SampleMask.assign(Copy.pSampleMask, Copy.pSampleMask + MaskWords);
    }
    Copy.pNext = nullptr;
    Copy.pSampleMask = nullptr;
    Out.MultisampleState = Copy;
  }

  // (roadmap H29n) This library may itself be built by linking other
  // libraries. Flatten their state in now, at capture time, so that every
  // consumer of a `GraphicsPipelineLibraryState` -- above all
  // `synthesizeLinkedGraphicsPipelineCreateInfo` -- sees one library's
  // complete subtree without having to walk a tree of its own.
  for (const auto *Next =
           static_cast<const VkBaseInStructure *>(CreateInfo.pNext);
       Next; Next = Next->pNext) {
    if (Next->sType != VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR)
      continue;
    const auto *LinkInfo =
        reinterpret_cast<const VkPipelineLibraryCreateInfoKHR *>(Next);
    for (uint32_t I = 0; I != LinkInfo->libraryCount; ++I) {
      auto *P = fromHandle<Pipeline>(LinkInfo->pLibraries[I]);
      if (P && P->kind() == Pipeline::Kind::GraphicsLibrary)
        foldLinkedLibraryState(
            Out, static_cast<const GraphicsPipelineLibrary *>(P)->state());
    }
    break;
  }

  return Out;
}

namespace {

/// (roadmap H29c) Every piece of storage a synthesized
/// `VkGraphicsPipelineCreateInfo` built by
/// `synthesizeLinkedGraphicsPipelineCreateInfo` points into. Owned by that
/// function's caller for exactly as long as the synthesized CreateInfo is
/// used (a single `compileGraphicsPipeline` call), mirroring the lifetime
/// contract a real, directly-supplied `VkGraphicsPipelineCreateInfo` and
/// everything it points to already have to satisfy for that same call.
/// `SpecInfos` is a `std::deque` (not a `std::vector`) specifically so
/// appending to it can never invalidate the address of an entry an earlier
/// `Stages` element's `pSpecializationInfo` already points to.
struct LinkedPipelineStorage {
  SmallVector<VkPipelineShaderStageCreateInfo, 4> Stages;
  SmallVector<VkDynamicState, 8> DynamicStates;
  VkPipelineDynamicStateCreateInfo DynamicStateInfo{};
  std::deque<VkSpecializationInfo> SpecInfos;
  /// (roadmap H29i) One entry per linked stage that used H29d's own
  /// inline-shader-module path; `std::deque` for the same
  /// address-stability reason `SpecInfos` is, since each entry's own
  /// `pCode` points at the *owning* `GraphicsPipelineLibraryStage`'s
  /// `InlineModuleWords` (stable for this call's duration) while the
  /// struct itself is what a `Stages` element's `pNext` points to.
  std::deque<VkShaderModuleCreateInfo> InlineModuleInfos;

  VkPipelineVertexInputStateCreateInfo VertexInputState{};
  VkPipelineInputAssemblyStateCreateInfo InputAssemblyState{};
  VkPipelineViewportStateCreateInfo ViewportState{};
  VkPipelineRasterizationStateCreateInfo RasterizationState{};
  VkPipelineTessellationStateCreateInfo TessellationState{};
  VkPipelineDepthStencilStateCreateInfo DepthStencilState{};
  VkPipelineColorBlendStateCreateInfo ColorBlendState{};
  VkPipelineMultisampleStateCreateInfo MultisampleState{};
  /// (roadmap H29m) Rebuilt from the fragment-output-interface library
  /// part's own captured `VkPipelineRenderingCreateInfo`, and chained onto
  /// the synthesized create info's `pNext` so `getRenderTargets` can find
  /// it. Held here rather than pointed at in the library part directly
  /// because its `pColorAttachmentFormats` must be re-aimed at that part's
  /// own owned format array.
  VkPipelineRenderingCreateInfo RenderingState{};
};

} // namespace

/// Appends one shader-stage entry to \p Storage's `Stages`, referencing \p
/// Stage's own already-owned `Name`/specialization storage directly --
/// valid for as long as the `GraphicsPipelineLibrary` object \p Stage was
/// captured from stays alive, guaranteed for the duration of the single
/// `vkCreateGraphicsPipelines` call this storage exists for.
static void addLinkedStage(LinkedPipelineStorage &Storage,
                           const GraphicsPipelineLibraryStage &Stage) {
  VkPipelineShaderStageCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Info.stage = Stage.Stage;
  Info.module = Stage.Module;
  Info.pName = Stage.Name.c_str();
  // (roadmap H29i) A null `Stage.Module` may still carry real shader code
  // this library part captured from H29d's own inline-shader-module path
  // (`captureLibraryStage`'s own `InlineModuleWords`); reconstruct the
  // equivalent chained `VkShaderModuleCreateInfo` so
  // `resolveShaderStageModule` compiles it exactly as it would have from
  // the application's own (by now possibly destroyed) original chain.
  if (!Stage.Module && !Stage.InlineModuleWords.empty()) {
    VkShaderModuleCreateInfo ModuleInfo{};
    ModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModuleInfo.codeSize = Stage.InlineModuleWords.size() * sizeof(uint32_t);
    ModuleInfo.pCode = Stage.InlineModuleWords.data();
    Storage.InlineModuleInfos.push_back(ModuleInfo);
    Info.pNext = &Storage.InlineModuleInfos.back();
  }
  if (!Stage.SpecMapEntries.empty()) {
    VkSpecializationInfo Spec{};
    Spec.mapEntryCount = static_cast<uint32_t>(Stage.SpecMapEntries.size());
    Spec.pMapEntries = Stage.SpecMapEntries.data();
    Spec.dataSize = Stage.SpecData.size();
    Spec.pData = Stage.SpecData.data();
    Storage.SpecInfos.push_back(Spec);
    Info.pSpecializationInfo = &Storage.SpecInfos.back();
  }
  Storage.Stages.push_back(Info);
}

/// Finds the linked library among \p Libraries whose own captured
/// `GraphicsPipelineLibraryState::Flags` includes \p Bit, or `nullptr` if
/// none does -- that part's state must then come directly from this
/// call's own `VkGraphicsPipelineCreateInfo` instead (the "partially
/// monolithic/partially library" mix the spec permits; see
/// `synthesizeLinkedGraphicsPipelineCreateInfo`'s own comment).
static const GraphicsPipelineLibrary *
findLinkedLibraryForBit(ArrayRef<GraphicsPipelineLibrary *> Libraries,
                        VkGraphicsPipelineLibraryFlagBitsEXT Bit) {
  for (const GraphicsPipelineLibrary *Lib : Libraries)
    if (Lib->state().Flags & Bit)
      return Lib;
  return nullptr;
}

/// (roadmap H29c) Synthesizes one complete `VkGraphicsPipelineCreateInfo`-
/// equivalent state for a non-library `vkCreateGraphicsPipelines` call
/// that links one or more `VK_EXT_graphics_pipeline_library` libraries via
/// a chained `VkPipelineLibraryCreateInfoKHR::pLibraries`, gathering each
/// of the four `VkGraphicsPipelineLibraryFlagBitsEXT` parts' state from
/// whichever linked library (roadmap H29b's own `GraphicsPipelineLibrary`
/// objects) provides it, or -- for a pipeline that is partially monolithic
/// and partially library, which the spec permits -- from \p CreateInfo's
/// own fields directly when no linked library supplies that part.
/// `PIPELINE_CONSTRUCTION_TYPE_LINK_TIME_OPTIMIZED_LIBRARY` and
/// `FAST_LINKED_LIBRARY` are treated identically: this CPU-emulated ICD
/// has no genuine fast-vs-optimized-link tradeoff for either to honor, so
/// nothing here distinguishes them. The caller passes the result to the
/// existing, unmodified `compileGraphicsPipeline`, exactly as it already
/// does for a directly-supplied monolithic `VkGraphicsPipelineCreateInfo`.
///
/// The returned `VkGraphicsPipelineCreateInfo`'s pointers reference only
/// \p Storage (owned by the caller for the scope of a single
/// `compileGraphicsPipeline` call) and each linked library's own already-
/// owned state (valid for as long as that pipeline handle exists,
/// guaranteed for the duration of this call) -- never \p CreateInfo's own
/// pointers reinterpreted into longer-lived storage, so nothing here
/// depends on \p CreateInfo outliving this function beyond its own,
/// already-existing lifetime guarantee.
static Expected<VkGraphicsPipelineCreateInfo>
synthesizeLinkedGraphicsPipelineCreateInfo(
    const VkGraphicsPipelineCreateInfo &CreateInfo,
    ArrayRef<VkPipeline> LibraryHandles, LinkedPipelineStorage &Storage) {
  SmallVector<GraphicsPipelineLibrary *, 4> Libraries;
  for (VkPipeline Handle : LibraryHandles) {
    auto *P = fromHandle<Pipeline>(Handle);
    if (!P || P->kind() != Pipeline::Kind::GraphicsLibrary)
      return createStringError(
          inconvertibleErrorCode(),
          "VkPipelineLibraryCreateInfoKHR::pLibraries must each name a "
          "VK_EXT_graphics_pipeline_library pipeline library");
    Libraries.push_back(static_cast<GraphicsPipelineLibrary *>(P));
  }

  VkGraphicsPipelineCreateInfo Result{};
  Result.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Result.flags = CreateInfo.flags;

  // Layout/RenderPass/Subpass are shared by every part per the spec's own
  // "Multiple Pipeline Creation" table; prefer this call's own value when
  // it supplies one non-null, since the final link is the one call that
  // must name the pipeline's real, complete VkPipelineLayout regardless of
  // which parts came from libraries -- falling back to a linked library's
  // own captured value only covers a pure-link call that supplies none of
  // its own state directly.
  Result.layout = CreateInfo.layout;
  Result.renderPass = CreateInfo.renderPass;
  Result.subpass = CreateInfo.subpass;
  auto addDynamicStates = [&](const VkPipelineDynamicStateCreateInfo *Info) {
    if (!Info)
      return;
    for (uint32_t I = 0; I != Info->dynamicStateCount; ++I)
      if (llvm::find(Storage.DynamicStates, Info->pDynamicStates[I]) ==
          Storage.DynamicStates.end())
        Storage.DynamicStates.push_back(Info->pDynamicStates[I]);
  };
  addDynamicStates(CreateInfo.pDynamicState);
  for (const GraphicsPipelineLibrary *Lib : Libraries) {
    if (!Result.layout)
      Result.layout = Lib->state().Layout;
    if (!Result.renderPass)
      Result.renderPass = Lib->state().RenderPass;
    for (VkDynamicState State : Lib->state().DynamicStates)
      if (llvm::find(Storage.DynamicStates, State) ==
          Storage.DynamicStates.end())
        Storage.DynamicStates.push_back(State);
  }
  if (!Storage.DynamicStates.empty()) {
    Storage.DynamicStateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    Storage.DynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(Storage.DynamicStates.size());
    Storage.DynamicStateInfo.pDynamicStates = Storage.DynamicStates.data();
    Result.pDynamicState = &Storage.DynamicStateInfo;
  }

  // Vertex input interface.
  if (const GraphicsPipelineLibrary *Lib = findLinkedLibraryForBit(
          Libraries,
          VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT)) {
    const GraphicsPipelineLibraryState &S = Lib->state();
    Storage.VertexInputState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    Storage.VertexInputState.vertexBindingDescriptionCount =
        static_cast<uint32_t>(S.VertexBindings.size());
    Storage.VertexInputState.pVertexBindingDescriptions =
        S.VertexBindings.empty() ? nullptr : S.VertexBindings.data();
    Storage.VertexInputState.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(S.VertexAttributes.size());
    Storage.VertexInputState.pVertexAttributeDescriptions =
        S.VertexAttributes.empty() ? nullptr : S.VertexAttributes.data();
    Result.pVertexInputState = &Storage.VertexInputState;
    if (S.InputAssembly) {
      Storage.InputAssemblyState = *S.InputAssembly;
      Result.pInputAssemblyState = &Storage.InputAssemblyState;
    }
  } else {
    Result.pVertexInputState = CreateInfo.pVertexInputState;
    Result.pInputAssemblyState = CreateInfo.pInputAssemblyState;
  }

  // Pre-rasterization shaders.
  if (const GraphicsPipelineLibrary *Lib = findLinkedLibraryForBit(
          Libraries,
          VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT)) {
    const GraphicsPipelineLibraryState &S = Lib->state();
    for (const GraphicsPipelineLibraryStage &Stage : S.PreRasterizationStages)
      addLinkedStage(Storage, Stage);
    if (S.ViewportState) {
      Storage.ViewportState = *S.ViewportState;
      Storage.ViewportState.viewportCount =
          static_cast<uint32_t>(S.Viewports.size());
      Storage.ViewportState.pViewports =
          S.Viewports.empty() ? nullptr : S.Viewports.data();
      Storage.ViewportState.scissorCount =
          static_cast<uint32_t>(S.Scissors.size());
      Storage.ViewportState.pScissors =
          S.Scissors.empty() ? nullptr : S.Scissors.data();
      Result.pViewportState = &Storage.ViewportState;
    }
    if (S.RasterizationState) {
      Storage.RasterizationState = *S.RasterizationState;
      Result.pRasterizationState = &Storage.RasterizationState;
    }
    if (S.TessellationState) {
      Storage.TessellationState = *S.TessellationState;
      Result.pTessellationState = &Storage.TessellationState;
    }
  } else {
    for (uint32_t I = 0; I != CreateInfo.stageCount; ++I)
      if (CreateInfo.pStages[I].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
        Storage.Stages.push_back(CreateInfo.pStages[I]);
    Result.pViewportState = CreateInfo.pViewportState;
    Result.pRasterizationState = CreateInfo.pRasterizationState;
    Result.pTessellationState = CreateInfo.pTessellationState;
  }

  // Fragment shader.
  const GraphicsPipelineLibrary *FragLib = findLinkedLibraryForBit(
      Libraries, VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT);
  if (FragLib) {
    const GraphicsPipelineLibraryState &S = FragLib->state();
    if (S.FragmentStage)
      addLinkedStage(Storage, *S.FragmentStage);
    if (S.DepthStencilState) {
      Storage.DepthStencilState = *S.DepthStencilState;
      Result.pDepthStencilState = &Storage.DepthStencilState;
    }
  } else {
    for (uint32_t I = 0; I != CreateInfo.stageCount; ++I)
      if (CreateInfo.pStages[I].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
        Storage.Stages.push_back(CreateInfo.pStages[I]);
    Result.pDepthStencilState = CreateInfo.pDepthStencilState;
  }

  // Fragment output interface.
  const GraphicsPipelineLibrary *OutLib = findLinkedLibraryForBit(
      Libraries,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT);
  if (OutLib) {
    const GraphicsPipelineLibraryState &S = OutLib->state();
    if (S.ColorBlendState) {
      Storage.ColorBlendState = *S.ColorBlendState;
      Storage.ColorBlendState.attachmentCount =
          static_cast<uint32_t>(S.ColorBlendAttachments.size());
      Storage.ColorBlendState.pAttachments =
          S.ColorBlendAttachments.empty() ? nullptr
                                          : S.ColorBlendAttachments.data();
      Result.pColorBlendState = &Storage.ColorBlendState;
    }
    // (roadmap H29m) Re-chain this part's own captured
    // `VkPipelineRenderingCreateInfo`, the dynamic-rendering counterpart of
    // the `RenderPass` handle merged above. Without this a linked pipeline
    // whose render target was declared only through dynamic rendering
    // reaches `getRenderTargets` with neither a render pass nor a rendering
    // create info and is rejected outright, even though the
    // fragment-output-interface part named its formats perfectly well.
    if (S.RenderingCreateInfo) {
      Storage.RenderingState = *S.RenderingCreateInfo;
      Storage.RenderingState.colorAttachmentCount =
          static_cast<uint32_t>(S.ColorAttachmentFormats.size());
      Storage.RenderingState.pColorAttachmentFormats =
          S.ColorAttachmentFormats.empty() ? nullptr
                                           : S.ColorAttachmentFormats.data();
      Result.pNext = &Storage.RenderingState;
    }
  } else {
    Result.pColorBlendState = CreateInfo.pColorBlendState;
  }

  // Multisample state is shared by the fragment-shader/fragment-output-
  // interface parts alike (same spec table); prefer whichever of the two
  // linked libraries actually captured one, falling back to this call's
  // own directly-supplied state when neither did.
  const GraphicsPipelineLibrary *MultisampleLib =
      (FragLib && FragLib->state().MultisampleState) ? FragLib
      : (OutLib && OutLib->state().MultisampleState) ? OutLib
                                                     : nullptr;
  if (MultisampleLib) {
    const GraphicsPipelineLibraryState &S = MultisampleLib->state();
    Storage.MultisampleState = *S.MultisampleState;
    Storage.MultisampleState.pSampleMask =
        S.SampleMask.empty() ? nullptr : S.SampleMask.data();
    Result.pMultisampleState = &Storage.MultisampleState;
  } else {
    Result.pMultisampleState = CreateInfo.pMultisampleState;
  }

  Result.stageCount = static_cast<uint32_t>(Storage.Stages.size());
  Result.pStages = Storage.Stages.empty() ? nullptr : Storage.Stages.data();
  return Result;
}

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
  if (isDynamic(DynamicStateDepthBiasEnable))
    ResolvedRaster.DepthBiasEnable = Dynamic.DepthBiasEnable;
  if (isDynamic(DynamicStateRasterizerDiscardEnable))
    ResolvedRaster.DiscardEnable = Dynamic.RasterizerDiscardEnable;
  // (roadmap H7d) `VK_DYNAMIC_STATE_DEPTH_BIAS`: like `LineWidth` above,
  // this is `RasterState::DepthBiasEnable`'s own static path made dynamic,
  // not a new feature.
  if (isDynamic(DynamicStateDepthBias)) {
    ResolvedRaster.DepthBiasConstantFactor = Dynamic.DepthBiasConstantFactor;
    ResolvedRaster.DepthBiasClamp = Dynamic.DepthBiasClamp;
    ResolvedRaster.DepthBiasSlopeFactor = Dynamic.DepthBiasSlopeFactor;
  }

  feme::graphics::DepthState ResolvedDepth = State.Depth;
  if (isDynamic(DynamicStateDepthTestEnable))
    ResolvedDepth.TestEnable = Dynamic.DepthTestEnable;
  if (isDynamic(DynamicStateDepthWriteEnable))
    ResolvedDepth.WriteEnable = Dynamic.DepthWriteEnable;
  if (isDynamic(DynamicStateDepthCompareOp))
    ResolvedDepth.Compare = Dynamic.DepthCompare;
  // (roadmap H7d) `Dynamic.DepthBoundsTestEnable` is now consulted for
  // real: `translateDepthStencilState` leaves `State.Depth.BoundsTestEnable`
  // at its default (`false`) whenever this state is dynamic, exactly like
  // `DepthCompareOp`'s own `CompareDynamic` handling above.
  if (isDynamic(DynamicStateDepthBoundsTestEnable))
    ResolvedDepth.BoundsTestEnable = Dynamic.DepthBoundsTestEnable;
  if (isDynamic(DynamicStateDepthBounds)) {
    ResolvedDepth.MinDepthBounds = Dynamic.MinDepthBounds;
    ResolvedDepth.MaxDepthBounds = Dynamic.MaxDepthBounds;
  }

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
      State.PrimitiveRestartEnable, State.SampleShadingEnable,
      State.AlphaToOneEnable, State.AlphaToCoverageEnable);
  // (roadmap H4b) `Artifact->HullStage` is set exactly when this pipeline
  // declared tessellation stages (see `compileAndValidateStages`'s own
  // comment); `PatchConstantStage`/`DomainStage` are always set alongside
  // it, and `State.Tessellation` was filled in and validated there too.
  if (State.Artifact->HullStage)
    Result.setTessellationStages(
        State.Artifact->HullStage, State.Artifact->PatchConstantStage,
        State.Artifact->DomainStage, State.Tessellation);
  // (roadmap H5e) `Artifact->GeometryStage` is set exactly when this
  // pipeline declared a geometry stage (see `compileAndValidateStages`'s
  // own comment), and `State.Geometry` was filled in and validated there
  // too.
  if (State.Artifact->GeometryStage)
    Result.setGeometryStage(State.Artifact->GeometryStage, State.Geometry);
  // (roadmap H6f) `Artifact->MeshStage` is set exactly when this pipeline
  // is a mesh pipeline (see `compileAndValidateStages`'s own comment), and
  // `State.Mesh` was filled in and validated there too. `Artifact->
  // TaskStage` is separately optional even for a mesh pipeline (a mesh
  // shader may be dispatched with no task stage driving it).
  // `MaxMeshWorkGroupCount`/`MaxMeshWorkGroupTotalCount` and their task
  // counterparts are this ICD's own honest, enforced dispatch ceilings
  // (`GraphicsPipeline.h`'s own comment), advertised verbatim by
  // `VkPhysicalDeviceMeshShaderPropertiesEXT` (`EntryPoints.cpp`) so the
  // two can never disagree.
  if (State.Artifact->MeshStage)
    Result.setMeshStage(
        State.Artifact->TaskStage, State.Artifact->MeshStage, State.Mesh,
        feme::graphics::AmplificationDispatchLimits{MaxMeshWorkGroupCount,
                                                    MaxMeshWorkGroupTotalCount},
        feme::graphics::AmplificationDispatchLimits{MaxTaskWorkGroupCount,
                                                    MaxTaskWorkGroupTotalCount},
        MaxTaskPayloadBytes);
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
  // (roadmap L89c) Consulted whether or not the app supplied a cache.
  PipelineCache &ImplicitCache =
      fromHandle<Device>(device)->getImplicitPipelineCache();
  Allocator Alloc(pAllocator);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != createInfoCount; ++I) {
    pPipelines[I] = VK_NULL_HANDLE;
    // (roadmap H29b) `VK_PIPELINE_CREATE_LIBRARY_BIT_KHR` marks this call
    // as creating a `VK_EXT_graphics_pipeline_library` pipeline library
    // rather than a complete, executable pipeline: capture whichever
    // `VkGraphicsPipelineLibraryCreateInfoEXT::flags` parts this call
    // provides (deep-copied, since `pCreateInfos[I]` need not outlive this
    // call) into a `GraphicsPipelineLibrary` object instead of running the
    // full `compileGraphicsPipeline` path below, which requires a
    // pipeline's *complete* state to be present all at once. Not yet
    // linkable into anything executable (roadmap H29c); still safe to
    // recognize unconditionally the same way every other H21-series
    // "decoder complete but unwired" struct was, since the extension
    // itself stays unadvertised (H29a) regardless of what this call
    // recognizes.
    if (pCreateInfos[I].flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
      VkGraphicsPipelineLibraryFlagsEXT LibraryFlags = 0;
      for (const auto *Next =
               static_cast<const VkBaseInStructure *>(pCreateInfos[I].pNext);
           Next; Next = Next->pNext) {
        if (Next->sType !=
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT)
          continue;
        LibraryFlags =
            reinterpret_cast<const VkGraphicsPipelineLibraryCreateInfoEXT *>(
                Next)
                ->flags;
        break;
      }
      fillPipelineCreationFeedback(pCreateInfos[I].pNext,
                                   pCreateInfos[I].stageCount,
                                   /*CacheHit=*/false);
      GraphicsPipelineLibrary *Obj = Alloc.create<GraphicsPipelineLibrary>(
          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
          captureGraphicsPipelineLibraryState(pCreateInfos[I], LibraryFlags),
          pCreateInfos[I].flags);
      if (!Obj) {
        Result = VK_ERROR_OUT_OF_HOST_MEMORY;
        continue;
      }
      pPipelines[I] = toHandle<VkPipeline>(static_cast<Pipeline *>(Obj));
      continue;
    }
    // (roadmap H29c) A non-library call may chain a
    // `VkPipelineLibraryCreateInfoKHR` naming one or more of roadmap
    // H29b's own `GraphicsPipelineLibrary` objects to link against,
    // instead of (or alongside) supplying every part's state directly.
    // `LinkedInfo`'s own storage must outlive the `compileGraphicsPipeline`
    // call below, so both live in this same loop iteration's scope.
    const VkPipelineLibraryCreateInfoKHR *LinkInfo = nullptr;
    for (const auto *Next =
             static_cast<const VkBaseInStructure *>(pCreateInfos[I].pNext);
         Next; Next = Next->pNext) {
      if (Next->sType != VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR)
        continue;
      LinkInfo = reinterpret_cast<const VkPipelineLibraryCreateInfoKHR *>(Next);
      break;
    }
    LinkedPipelineStorage LinkedStorage;
    VkGraphicsPipelineCreateInfo LinkedInfo{};
    const VkGraphicsPipelineCreateInfo *EffectiveInfo = &pCreateInfos[I];
    if (LinkInfo && LinkInfo->libraryCount != 0) {
      Expected<VkGraphicsPipelineCreateInfo> Synthesized =
          synthesizeLinkedGraphicsPipelineCreateInfo(
              pCreateInfos[I],
              ArrayRef(LinkInfo->pLibraries, LinkInfo->libraryCount),
              LinkedStorage);
      if (!Synthesized) {
        logCreationFailure(Synthesized.takeError(),
                           "vkCreateGraphicsPipelines");
        Result = VK_ERROR_INITIALIZATION_FAILED;
        continue;
      }
      LinkedInfo = *Synthesized;
      EffectiveInfo = &LinkedInfo;
    }
    bool CacheHit = false;
    Expected<std::optional<GraphicsPipelineState>> Compiled =
        compileGraphicsPipeline(*EffectiveInfo, DeviceInfo, Cache,
                                ImplicitCache, CacheHit);
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
