//===- GraphicsPipeline.h - VkPipeline graphics state ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) The graphics `VkPipeline` (see "Graphics pipeline state" in
// feme/docs/FeMeVulkanDesign.md): `vkCreateGraphicsPipelines` compiles each
// stage through the same flow the compute path uses -- with
// `feme::cpu::StageCompileOptions` naming the stage -- and translates the
// fixed-function state into `feme::graphics`' normalized pipeline
// description.
//
// The translated state is stored here rather than as a ready-made
// `feme::graphics::GraphicsPipeline` because dynamic state is what makes a
// prepared draw a snapshot rather than a pipeline pointer: the executor
// pipeline is built per draw by `buildExecutorPipeline`, folding in whatever
// `vkCmdSet*` last recorded for the state this pipeline declared dynamic.
//
// Anything with no implemented path fails here, at creation: "A pipeline
// whose state combination has no implemented path must also fail at
// creation; a draw is not permitted to be the place a state combination is
// discovered to be unsupported."
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_GRAPHICSPIPELINE_H
#define FEME_LIB_VULKAN_GRAPHICSPIPELINE_H

#include "PhysicalDeviceInfo.h"
#include "Pipeline.h"

#include "feme/Graphics/AmplificationDispatch.h"
#include "feme/Graphics/Geometry.h"
#include "feme/Graphics/Mesh.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace feme {
class Context;
} // namespace feme

namespace feme::vulkan {

/// One `VkVertexInputBindingDescription`, normalized.
struct VertexInputBinding {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
  /// Whether this binding advances per vertex or per instance
  /// (`VkVertexInputRate`).
  bool PerInstance = false;
  /// (roadmap F6) `VK_KHR_vertex_attribute_divisor`'s per-binding instance
  /// divisor: only meaningful when `PerInstance` is set, `1` (the default)
  /// matches core 1.0's "advance once per instance" behavior exactly, and
  /// `0` (`vertexAttributeInstanceRateZeroDivisor`) means every instance
  /// reads the same vertex, at `firstInstance`.
  uint32_t Divisor = 1;
};

/// One `VkVertexInputAttributeDescription`, normalized: its
/// `VkFormat` already resolved through the central format table.
struct VertexInputAttribute {
  uint32_t Location = 0;
  uint32_t Binding = 0;
  uint32_t Offset = 0;
  feme::cpu::ResourceFormat Format = feme::cpu::ResourceFormat::Unknown;
};

/// (roadmap F6) `VkPhysicalDeviceVertexAttributeDivisorProperties::
/// maxVertexAttribDivisor`: the largest per-binding divisor
/// `translateVertexInput` accepts. The fetch-index computation this divisor
/// feeds (`firstInstance + (instanceIndex - firstInstance) / divisor`,
/// `Executor.cpp`) is a plain 32-bit integer divide with no narrower bound
/// of its own, so the full range is a genuine, verified limit rather than a
/// conservative placeholder. Shared between `GraphicsPipeline.cpp`'s
/// validation and `EntryPoints.cpp`'s advertised property so the two can
/// never disagree.
constexpr uint32_t MaxVertexAttribDivisor = 0xFFFFFFFFu;

/// (roadmap H6f) `VkPhysicalDeviceMeshShaderPropertiesEXT::
/// maxMeshOutputVertices`/`maxMeshOutputPrimitives`: shared between
/// `compileAndValidateStages`'s own creation-time enforcement of a compiled
/// mesh entry's declared `MeshState::MaxOutputVertices`/`MaxOutputPrimitives`
/// and `EntryPoints.cpp`'s advertised property, so the two can never
/// disagree -- exactly as `MaxVertexAttribDivisor` above does for its own
/// property/validation pair. Matches `maxGeometryOutputVertices`'s own
/// honest ceiling (H5e): mesh output assembly (`MeshOutputBuilder`) has no
/// larger fixed-size buffer than geometry's own.
constexpr uint32_t MaxMeshOutputVertices = 256;
constexpr uint32_t MaxMeshOutputPrimitives = 256;

/// (roadmap H6f) `maxMeshWorkGroupCount`/`maxMeshWorkGroupTotalCount` and
/// their task-stage counterparts (`maxTaskWorkGroupCount`/
/// `maxTaskWorkGroupTotalCount`): shared between `buildExecutorPipeline`'s
/// own `feme::graphics::AmplificationDispatchLimits` (enforced at draw time
/// by `AmplificationDispatchQueue`, see `Executor.cpp`) and
/// `EntryPoints.cpp`'s advertised properties, mirroring
/// `maxComputeWorkGroupCount`/`maxComputeWorkGroupTotalCount` exactly: the
/// mesh and task stages reuse compute's own group-dispatch machinery
/// unmodified (roadmap H6c), so compute's own real, enforced limits are a
/// legitimately honest mirror here rather than an inflated guess.
constexpr std::array<uint32_t, 3> MaxMeshWorkGroupCount = {65535, 65535, 65535};
constexpr uint32_t MaxMeshWorkGroupTotalCount = 4194304;
constexpr std::array<uint32_t, 3> MaxTaskWorkGroupCount = {65535, 65535, 65535};
constexpr uint32_t MaxTaskWorkGroupTotalCount = 4194304;

/// The `VkDynamicState` subset this driver implements, as a bitmask. A
/// pipeline naming any other dynamic state fails creation (see the file
/// comment above), since silently treating it as static would render the
/// wrong thing.
enum DynamicStateBits : uint32_t {
  DynamicStateViewport = 1u << 0,
  DynamicStateScissor = 1u << 1,
  DynamicStateBlendConstants = 1u << 2,
  DynamicStateStencilReference = 1u << 3,
  DynamicStateStencilCompareMask = 1u << 4,
  DynamicStateStencilWriteMask = 1u << 5,
  // (roadmap C4c) `VK_EXT_extended_dynamic_state`'s states. Every one of
  // these already has a real, fully-implemented *static* path (cull mode,
  // front face, depth test/write/compare, stencil test/op); making each
  // dynamic is the same "read from the per-draw snapshot instead of the
  // pipeline's own creation-time value" pattern the six states above
  // already use, not a new rasterizer feature -- unlike mapTopology's
  // still-open point/line topologies or the dual-source blend factors (see
  // FeMeGraphicsDesign.md's status note). `DynamicStateDepthBoundsTest
  // Enable` is the one exception worth calling out: the depth bounds test
  // itself is not implemented, but the `depthBounds` feature this ICD
  // advertises is also `VK_FALSE` (PhysicalDeviceInfo.cpp), so a
  // conformant application can never legally set this dynamic state to
  // `VK_TRUE` in the first place -- it is accepted and stored but never
  // consulted, exactly as `depthBoundsTestEnable` in the static path
  // already isn't (see `translateDepthStencilState`'s rejection of it).
  DynamicStateCullMode = 1u << 6,
  DynamicStateFrontFace = 1u << 7,
  DynamicStateDepthTestEnable = 1u << 8,
  DynamicStateDepthWriteEnable = 1u << 9,
  DynamicStateDepthCompareOp = 1u << 10,
  DynamicStateDepthBoundsTestEnable = 1u << 11,
  DynamicStateStencilTestEnable = 1u << 12,
  DynamicStateStencilOp = 1u << 13,
  // `VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY`: unlike every other state above,
  // Vulkan does *not* say the pipeline's static `topology` becomes
  // irrelevant once this is dynamic -- it still fixes the topology
  // *class* (point/line/triangle) every value set at draw time must
  // share. Since `mapTopology` only ever accepts the triangle class
  // (`TriangleList`/`TriangleStrip`), that requirement is automatically
  // satisfied here: `translateFixedFunctionState`'s existing topology
  // translation is untouched by this state at all.
  DynamicStatePrimitiveTopology = 1u << 14,
  // `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`/`_SCISSOR_WITH_COUNT` reuse
  // `DynamicStateViewport`/`DynamicStateScissor` above rather than adding
  // their own bits -- see `mapDynamicState`'s comment.
  // `VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE`: set via
  // `vkCmdBindVertexBuffers2EXT`'s optional `pStrides` (`CommandBuffer.h`'s
  // `GraphicsState::VertexBufferStrides`) rather than through
  // `DynamicGraphicsState` -- it is bound-buffer state, tracked alongside
  // the buffers/offsets `vkCmdBindVertexBuffers` itself already carries,
  // not a fixed-size per-draw snapshot field like every other state here.
  DynamicStateVertexInputBindingStride = 1u << 15,
  // (roadmap F5) `VK_DYNAMIC_STATE_LINE_WIDTH` (core 1.0) and
  // `VK_DYNAMIC_STATE_LINE_STIPPLE_KHR`
  // (`VK_KHR_line_rasterization`): `vkCmdSetLineWidth`'s payload
  // previously had nowhere to go (the entry point was a no-op stub) and
  // `vkCmdSetLineStippleKHR` is new; both now flow through
  // `DynamicGraphicsState` exactly like every other per-draw state above.
  DynamicStateLineWidth = 1u << 16,
  DynamicStateLineStipple = 1u << 17,
};

/// The command-buffer-resolved value of every piece of dynamic state a
/// graphics pipeline may declare, snapshotted into each draw (see "Dynamic
/// state is what makes the prepared draw a snapshot rather than a pipeline
/// pointer"). A pipeline that declared a given state *static* ignores the
/// corresponding member here and uses its own creation-time value instead.
struct DynamicGraphicsState {
  DynamicGraphicsState() {
    Viewports.push_back(feme::graphics::ViewportState{});
    Scissors.push_back(feme::graphics::ScissorRect{});
  }

  llvm::SmallVector<feme::graphics::ViewportState, MaxViewportCount> Viewports;
  llvm::SmallVector<feme::graphics::ScissorRect, MaxViewportCount> Scissors;
  std::array<float, 4> BlendConstants{0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t StencilReference[2] = {0, 0}; // [front, back]
  uint32_t StencilCompareMask[2] = {0xFF, 0xFF};
  uint32_t StencilWriteMask[2] = {0xFF, 0xFF};
  feme::graphics::CullMode Cull = feme::graphics::CullMode::None;
  feme::graphics::FrontFace Front = feme::graphics::FrontFace::CounterClockwise;
  bool DepthTestEnable = false;
  bool DepthWriteEnable = false;
  feme::graphics::CompareOp DepthCompare = feme::graphics::CompareOp::Less;
  // `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE`'s own value: stored for
  // completeness but never consulted (see `DynamicStateBits`'s comment).
  bool DepthBoundsTestEnable = false;
  bool StencilTestEnable = false;
  /// `vkCmdSetStencilOpEXT`'s per-face payload ([front, back], like the
  /// other per-face stencil dynamic state above): unlike
  /// `StencilReference`/`*Mask`, which only ever override one
  /// `StencilFaceState` field at a time, this one call sets all three ops
  /// plus the compare op together.
  struct StencilOpState {
    feme::graphics::StencilOp FailOp = feme::graphics::StencilOp::Keep;
    feme::graphics::StencilOp PassOp = feme::graphics::StencilOp::Keep;
    feme::graphics::StencilOp DepthFailOp = feme::graphics::StencilOp::Keep;
    feme::graphics::CompareOp Compare = feme::graphics::CompareOp::Always;
  };
  StencilOpState StencilOps[2]; // [front, back]
  /// `vkCmdSetPrimitiveTopologyEXT`'s payload, already mapped through
  /// `mapTopology` -- `std::nullopt` when the value it was last set to is
  /// outside the triangle class this executor implements (a pipeline may
  /// only legally do this if its own static topology is also triangle-
  /// class, so a conformant caller never actually produces `nullopt`
  /// here; `buildExecutorPipeline` falls back to the pipeline's own
  /// static topology in that defensive case rather than rendering with an
  /// unspecified one).
  std::optional<feme::graphics::PrimitiveTopology> Topology;
  /// (roadmap F5) `vkCmdSetLineWidth`'s payload.
  float LineWidth = 1.0f;
  /// (roadmap F5) `vkCmdSetLineStippleKHR`'s payload.
  uint32_t StippleFactor = 1;
  uint16_t StipplePattern = 0xFFFF;
};

/// The shareable, compiled part of a graphics `VkPipeline`: the
/// `feme::Context` both stages were JIT-ed into and the two compiled stages
/// themselves, kept alive for as long as any `VkPipeline` handle references
/// them (the same rule `CachedPipelineArtifact` states for compute).
struct GraphicsPipelineArtifact {
  std::unique_ptr<feme::Context> Ctx;
  std::shared_ptr<feme::cpu::CompiledStage> VertexStage;
  std::shared_ptr<feme::cpu::CompiledStage> FragmentStage;
  /// (roadmap H4b) Set only for a pipeline declaring
  /// `VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT`/`_EVALUATION_BIT`: the
  /// tessellation-control module's two split phases (roadmap H4a's control-
  /// point phase, kept under `HullStage`, and its `PatchConstantStage`
  /// sibling) plus the tessellation-evaluation module's `DomainStage`.
  /// Either all three are set, or none are.
  std::shared_ptr<feme::cpu::CompiledStage> HullStage;
  std::shared_ptr<feme::cpu::CompiledStage> PatchConstantStage;
  std::shared_ptr<feme::cpu::CompiledStage> DomainStage;
  /// (roadmap H5e) Set only for a pipeline declaring
  /// `VK_SHADER_STAGE_GEOMETRY_BIT`.
  std::shared_ptr<feme::cpu::CompiledStage> GeometryStage;
  /// (roadmap H6f) Set only for a mesh pipeline (declaring
  /// `VK_SHADER_STAGE_MESH_BIT_EXT`); mutually exclusive with every stage
  /// above (`VertexStage`, the tessellation/geometry stages) -- a graphics
  /// pipeline is either a "primitive" pipeline (vertex, optionally
  /// tessellation/geometry) or a mesh pipeline, never a mix of the two.
  std::shared_ptr<feme::cpu::CompiledStage> MeshStage;
  /// (roadmap H6f) Set only when `MeshStage` is and the pipeline also
  /// declares `VK_SHADER_STAGE_TASK_BIT_EXT`; the task stage is optional
  /// even for a mesh pipeline (a mesh shader may be dispatched directly,
  /// with no task stage driving it -- see `graphics::GraphicsPipeline::
  /// hasTaskStage`).
  std::shared_ptr<feme::cpu::CompiledStage> TaskStage;
};

/// One graphics pipeline's compiled stages plus its whole translated,
/// normalized fixed-function state -- everything `vkCreateGraphicsPipelines`
/// resolves once, at creation. A plain record: `GraphicsPipeline` below is
/// the Vulkan object that owns one.
struct GraphicsPipelineState {
  GraphicsPipelineState() {
    Viewports.push_back(feme::graphics::ViewportState{});
    Scissors.push_back(feme::graphics::ScissorRect{});
  }

  std::shared_ptr<GraphicsPipelineArtifact> Artifact;
  feme::graphics::PrimitiveTopology Topology =
      feme::graphics::PrimitiveTopology::TriangleList;
  /// `VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable`; only
  /// implemented for `TriangleStrip` (see `translateVertexInput`'s caller).
  bool PrimitiveRestartEnable = false;
  feme::graphics::RasterState Raster;
  feme::graphics::DepthState Depth;
  feme::graphics::StencilState Stencil;
  std::vector<feme::graphics::BlendState> ColorBlends;
  bool LogicOpEnable = false;
  feme::graphics::LogicOp Logic = feme::graphics::LogicOp::Copy;
  std::array<float, 4> BlendConstants{0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t SampleCount = 1;
  std::vector<feme::graphics::AttachmentFormat> Attachments;
  std::vector<VertexInputBinding> VertexBindings;
  std::vector<VertexInputAttribute> VertexAttributes;
  llvm::SmallVector<feme::graphics::ViewportState, MaxViewportCount> Viewports;
  llvm::SmallVector<feme::graphics::ScissorRect, MaxViewportCount> Scissors;
  uint32_t DynamicStates = 0;
  /// (roadmap F10) Each stage's own resolved `PipelineRobustness` -- see
  /// that struct's own comment in Pipeline.h.
  PipelineRobustness VertexRobustness;
  PipelineRobustness FragmentRobustness;
  /// (roadmap H4b) Set only when `Artifact->HullStage` is (i.e. the
  /// pipeline declares tessellation stages): the tessellator state
  /// `buildExecutorPipeline` hands to `graphics::GraphicsPipeline::
  /// setTessellationStages`, assembled from `VkPipelineTessellationState
  /// CreateInfo::patchControlPoints` (`InputControlPointCount`) and each
  /// compiled stage's own `feme.tessellation.*` reflection (everything
  /// else).
  feme::graphics::TessellationState Tessellation;
  /// (roadmap H5e) Set only when `Artifact->GeometryStage` is (i.e. the
  /// pipeline declares a geometry stage): the geometry shape
  /// `buildExecutorPipeline` hands to `graphics::GraphicsPipeline::
  /// setGeometryStage`, assembled entirely from the compiled stage's own
  /// `feme.geometry.*` reflection (unlike `Tessellation`, nothing here
  /// comes from a `VkGraphicsPipelineCreateInfo` field).
  feme::graphics::GeometryState Geometry;
  /// (roadmap H6f) Set only when `Artifact->MeshStage` is (i.e. the
  /// pipeline is a mesh pipeline): the mesh shape `buildExecutorPipeline`
  /// hands to `graphics::GraphicsPipeline::setMeshStage`, assembled
  /// entirely from the compiled mesh stage's own `feme.mesh.*` reflection
  /// (like `Geometry` above, nothing here comes from a
  /// `VkGraphicsPipelineCreateInfo` field).
  feme::graphics::MeshState Mesh;
};

/// A `VkPipeline` graphics pipeline: the compiled stages plus the
/// translated, normalized fixed-function state (see the file comment).
class GraphicsPipeline : public Pipeline {
public:
  explicit GraphicsPipeline(GraphicsPipelineState State,
                            VkPipelineCreateFlags CreateFlags = 0)
      : Pipeline(Kind::Graphics, CreateFlags), State(std::move(State)) {}

  /// Builds the executor pipeline description for one draw, resolving
  /// pipeline state and \p Dynamic together (see the file comment above).
  feme::graphics::GraphicsPipeline
  buildExecutorPipeline(const DynamicGraphicsState &Dynamic) const;

  /// The viewport/scissor a draw uses: this pipeline's own static values,
  /// or \p Dynamic's when the pipeline declared them dynamic.
  llvm::ArrayRef<feme::graphics::ViewportState>
  resolveViewport(const DynamicGraphicsState &Dynamic) const {
    return isDynamic(DynamicStateViewport)
               ? llvm::ArrayRef<feme::graphics::ViewportState>(
                     Dynamic.Viewports)
               : llvm::ArrayRef<feme::graphics::ViewportState>(State.Viewports);
  }

  llvm::ArrayRef<feme::graphics::ScissorRect>
  resolveScissor(const DynamicGraphicsState &Dynamic) const {
    return isDynamic(DynamicStateScissor)
               ? llvm::ArrayRef<feme::graphics::ScissorRect>(Dynamic.Scissors)
               : llvm::ArrayRef<feme::graphics::ScissorRect>(State.Scissors);
  }

  bool isDynamic(DynamicStateBits Bit) const {
    return (State.DynamicStates & Bit) != 0;
  }

  llvm::ArrayRef<VertexInputBinding> vertexBindings() const {
    return State.VertexBindings;
  }
  llvm::ArrayRef<VertexInputAttribute> vertexAttributes() const {
    return State.VertexAttributes;
  }
  uint32_t colorAttachmentCount() const {
    return static_cast<uint32_t>(State.ColorBlends.size());
  }
  uint32_t sampleCount() const { return State.SampleCount; }
  /// Whether a draw through this pipeline needs a bound depth attachment
  /// (`CommandBuffer.cpp`'s own draw-time check): true if the *static*
  /// state enables testing/writes, or if either is dynamic -- a
  /// dynamically-enabled test needs the same attachment `translate
  /// DepthStencilState` already required the render target to declare at
  /// creation time, even though the static booleans it reads by default
  /// here may both be false.
  bool needsDepthAttachment() const {
    return State.Depth.TestEnable || State.Depth.WriteEnable ||
           isDynamic(DynamicStateDepthTestEnable) ||
           isDynamic(DynamicStateDepthWriteEnable);
  }
  bool needsStencilAttachment() const {
    return State.Stencil.TestEnable || isDynamic(DynamicStateStencilTestEnable);
  }
  const feme::cpu::CompiledStage &vertexStage() const {
    return *State.Artifact->VertexStage;
  }
  /// Whether this pipeline has a fragment stage (roadmap H2j): false for a
  /// depth/stencil-only pipeline whose render target has no color
  /// attachments and that legally omitted one.
  bool hasFragmentStage() const {
    return State.Artifact->FragmentStage != nullptr;
  }
  /// Only valid to call when `hasFragmentStage()` is true.
  const feme::cpu::CompiledStage &fragmentStage() const {
    return *State.Artifact->FragmentStage;
  }
  /// (roadmap F10) See `PipelineRobustness`'s own comment in Pipeline.h.
  const PipelineRobustness &vertexRobustness() const {
    return State.VertexRobustness;
  }
  const PipelineRobustness &fragmentRobustness() const {
    return State.FragmentRobustness;
  }
  /// (roadmap H4b) Whether this pipeline declares tessellation stages.
  bool hasTessellationStages() const {
    return State.Artifact->HullStage != nullptr;
  }
  /// (roadmap H5e) Whether this pipeline declares a geometry stage.
  bool hasGeometryStages() const {
    return State.Artifact->GeometryStage != nullptr;
  }
  /// (roadmap H6f) Whether this is a mesh pipeline (declares
  /// `VK_SHADER_STAGE_MESH_BIT_EXT`, and so has no vertex/tessellation/
  /// geometry stages -- see `GraphicsPipelineArtifact`'s own comment).
  bool hasMeshStages() const { return State.Artifact->MeshStage != nullptr; }
  /// Only valid to call when `hasMeshStages()` is true.
  bool hasTaskStage() const { return State.Artifact->TaskStage != nullptr; }

private:
  GraphicsPipelineState State;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_GRAPHICSPIPELINE_H
