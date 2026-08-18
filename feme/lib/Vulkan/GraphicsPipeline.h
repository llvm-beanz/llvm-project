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

#include "Pipeline.h"

#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/PreparedDraw.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace feme {
class Context;
} // namespace feme

namespace feme::vulkan {

/// One `VkVertexInputBindingDescription`, normalized.
struct VertexInputBinding {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
};

/// One `VkVertexInputAttributeDescription`, normalized: its
/// `VkFormat` already resolved through the central format table.
struct VertexInputAttribute {
  uint32_t Location = 0;
  uint32_t Binding = 0;
  uint32_t Offset = 0;
  feme::cpu::ResourceFormat Format = feme::cpu::ResourceFormat::Unknown;
};

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
};

/// The command-buffer-resolved value of every piece of dynamic state a
/// graphics pipeline may declare, snapshotted into each draw (see "Dynamic
/// state is what makes the prepared draw a snapshot rather than a pipeline
/// pointer"). A pipeline that declared a given state *static* ignores the
/// corresponding member here and uses its own creation-time value instead.
struct DynamicGraphicsState {
  feme::graphics::ViewportState Viewport;
  feme::graphics::ScissorRect Scissor;
  std::array<float, 4> BlendConstants{0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t StencilReference[2] = {0, 0};   // [front, back]
  uint32_t StencilCompareMask[2] = {0xFF, 0xFF};
  uint32_t StencilWriteMask[2] = {0xFF, 0xFF};
};

/// The shareable, compiled part of a graphics `VkPipeline`: the
/// `feme::Context` both stages were JIT-ed into and the two compiled stages
/// themselves, kept alive for as long as any `VkPipeline` handle references
/// them (the same rule `CachedPipelineArtifact` states for compute).
struct GraphicsPipelineArtifact {
  std::unique_ptr<feme::Context> Ctx;
  std::shared_ptr<feme::cpu::CompiledStage> VertexStage;
  std::shared_ptr<feme::cpu::CompiledStage> FragmentStage;
};

/// One graphics pipeline's compiled stages plus its whole translated,
/// normalized fixed-function state -- everything `vkCreateGraphicsPipelines`
/// resolves once, at creation. A plain record: `GraphicsPipeline` below is
/// the Vulkan object that owns one.
struct GraphicsPipelineState {
  std::shared_ptr<GraphicsPipelineArtifact> Artifact;
  feme::graphics::PrimitiveTopology Topology =
      feme::graphics::PrimitiveTopology::TriangleList;
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
  feme::graphics::ViewportState Viewport;
  feme::graphics::ScissorRect Scissor;
  uint32_t DynamicStates = 0;
};

/// A `VkPipeline` graphics pipeline: the compiled stages plus the
/// translated, normalized fixed-function state (see the file comment).
class GraphicsPipeline : public Pipeline {
public:
  explicit GraphicsPipeline(GraphicsPipelineState State)
      : Pipeline(Kind::Graphics), State(std::move(State)) {}

  /// Builds the executor pipeline description for one draw, resolving
  /// pipeline state and \p Dynamic together (see the file comment above).
  feme::graphics::GraphicsPipeline
  buildExecutorPipeline(const DynamicGraphicsState &Dynamic) const;

  /// The viewport/scissor a draw uses: this pipeline's own static values,
  /// or \p Dynamic's when the pipeline declared them dynamic.
  feme::graphics::ViewportState
  resolveViewport(const DynamicGraphicsState &Dynamic) const {
    return isDynamic(DynamicStateViewport) ? Dynamic.Viewport : State.Viewport;
  }
  feme::graphics::ScissorRect
  resolveScissor(const DynamicGraphicsState &Dynamic) const {
    return isDynamic(DynamicStateScissor) ? Dynamic.Scissor : State.Scissor;
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
  bool needsDepthAttachment() const {
    return State.Depth.TestEnable || State.Depth.WriteEnable;
  }
  bool needsStencilAttachment() const { return State.Stencil.TestEnable; }
  const feme::cpu::CompiledStage &vertexStage() const {
    return *State.Artifact->VertexStage;
  }
  const feme::cpu::CompiledStage &fragmentStage() const {
    return *State.Artifact->FragmentStage;
  }

private:
  GraphicsPipelineState State;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_GRAPHICSPIPELINE_H
