//===- Pipeline.h - FeMe software graphics executor pipeline ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::GraphicsPipeline, the normalized,
// immutable pipeline description the software graphics executor consumes
// (roadmap R31, "FeMeGraphics skeleton" -- see feme/docs/Roadmap.md and the
// "Normalized pipeline" section of feme/docs/FeMeGraphicsDesign.md).
//
// This is a *description*, not the executor itself: it owns the compiled
// vertex/fragment stages plus every piece of state affecting how a draw is
// executed, but implements no clip/raster/interpolation logic. Roadmap R32
// ("Basic triangle pipeline") adds the executor that walks a
// `GraphicsPipeline`/`PreparedDraw` pair; see PreparedDraw.h for the
// per-draw dynamic state counterpart.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PIPELINE_H
#define FEME_GRAPHICS_PIPELINE_H

#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace feme::graphics {

/// How vertices are assembled into primitives. Only the two triangle
/// topologies have an executor yet (roadmap R32); the rest are recorded
/// here since a pipeline description must reject a topology it does not
/// implement rather than silently misinterpret it.
enum class PrimitiveTopology : uint8_t {
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleStrip,
};

/// Which primitive-facing direction, if any, is discarded before
/// rasterization.
enum class CullMode : uint8_t {
  None,
  Front,
  Back,
};

/// Which vertex winding order is front-facing.
enum class FrontFace : uint8_t {
  CounterClockwise,
  Clockwise,
};

/// A depth/stencil comparison function, shared by depth testing and (once
/// stencil lands, roadmap R33) stencil testing.
enum class CompareOp : uint8_t {
  Never,
  Less,
  Equal,
  LessEqual,
  Greater,
  NotEqual,
  GreaterEqual,
  Always,
};

/// How a fragment's color is combined with an attachment's existing value.
/// `Replace` (no blending) matches the scene YAML's own `blend: replace`
/// spelling ("Textual scene and image fixtures" in feme/docs/Design.md).
/// The executor's actual blend behavior is driven by `BlendState` (below,
/// part of `GraphicsPipeline`'s color-attachment state since roadmap R33);
/// this enum is retained only for the scene YAML's simple `blend: replace`
/// spelling and existing call sites that pass it directly.
enum class BlendMode : uint8_t {
  Replace,
};

/// One operand of a blend equation, matching Vulkan's `VkBlendFactor`/
/// Direct3D's `D3D12_BLEND` one-for-one (dual-source factors are not
/// modelled yet: no test needs a second fragment output).
enum class BlendFactor : uint8_t {
  Zero,
  One,
  SrcColor,
  OneMinusSrcColor,
  DstColor,
  OneMinusDstColor,
  SrcAlpha,
  OneMinusSrcAlpha,
  DstAlpha,
  OneMinusDstAlpha,
  ConstantColor,
  OneMinusConstantColor,
  ConstantAlpha,
  OneMinusConstantAlpha,
  SrcAlphaSaturate,
};

/// How a blend equation combines its two scaled operands, matching
/// Vulkan's `VkBlendOp`/Direct3D's `D3D12_BLEND_OP`.
enum class BlendOp : uint8_t {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
};

/// A bitwise logic operation between a fragment's integer color and an
/// attachment's existing value, matching Vulkan's `VkLogicOp`/Direct3D's
/// `D3D12_LOGIC_OP`. Mutually exclusive with blending: enabling a logic op
/// disables blending for every color attachment, per both APIs.
enum class LogicOp : uint8_t {
  Clear,
  Set,
  Copy,
  CopyInverted,
  NoOp,
  Invert,
  And,
  Nand,
  Or,
  Nor,
  Xor,
  Equivalent,
  AndReverse,
  AndInverted,
  OrReverse,
  OrInverted,
};

/// One color attachment's blend/write-mask state. `WriteMask` bit 0/1/2/3
/// gate the R/G/B/A channels respectively; a cleared bit leaves that
/// channel's stored value untouched regardless of `BlendEnable`.
/// `BlendEnable == false` is the `BlendMode::Replace` equation (the
/// fragment's color entirely replaces the masked channels).
struct BlendState {
  bool BlendEnable = false;
  BlendFactor SrcColorFactor = BlendFactor::One;
  BlendFactor DstColorFactor = BlendFactor::Zero;
  BlendOp ColorOp = BlendOp::Add;
  BlendFactor SrcAlphaFactor = BlendFactor::One;
  BlendFactor DstAlphaFactor = BlendFactor::Zero;
  BlendOp AlphaOp = BlendOp::Add;
  uint8_t WriteMask = 0xF;
};

/// What a passing/failing depth or stencil test does to a bound stencil
/// attachment's value, shared by both stencil faces (`StencilFaceState`).
enum class StencilOp : uint8_t {
  Keep,
  Zero,
  Replace,
  IncrementClamp,
  DecrementClamp,
  Invert,
  IncrementWrap,
  DecrementWrap,
};

/// One face's (front- or back-facing primitive's) stencil test/update
/// state, matching Vulkan's `VkStencilOpState`/Direct3D's
/// `D3D12_DEPTH_STENCILOP_DESC` one-for-one.
struct StencilFaceState {
  CompareOp Compare = CompareOp::Always;
  StencilOp FailOp = StencilOp::Keep;
  StencilOp DepthFailOp = StencilOp::Keep;
  StencilOp PassOp = StencilOp::Keep;
  uint8_t CompareMask = 0xFF;
  uint8_t WriteMask = 0xFF;
  uint8_t Reference = 0;
};

/// Stencil test/write state for one pipeline (roadmap R33): a bound
/// `S8_UINT` attachment (`DepthStencilAttachment::Stencil` in
/// PreparedDraw.h) is required whenever `TestEnable` is set, matching
/// `DepthState`'s own attachment requirement.
struct StencilState {
  bool TestEnable = false;
  StencilFaceState Front;
  StencilFaceState Back;
};

/// Depth test/write state for one pipeline.
struct DepthState {
  bool TestEnable = false;
  bool WriteEnable = false;
  CompareOp Compare = CompareOp::Less;
};

/// Fixed-function rasterization state that does not vary per draw.
struct RasterState {
  CullMode Cull = CullMode::None;
  FrontFace Front = FrontFace::CounterClockwise;
};

/// One attachment's format/extent identity, part of the pipeline's cache key
/// (see "Normalized pipeline"'s "attachment format classes" bullet).
struct AttachmentFormat {
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Width = 0;
  uint32_t Height = 0;
};

/// The normalized, immutable pipeline description the software graphics
/// executor consumes (see the file comment above): the compiled raster
/// stages, primitive topology, rasterization/depth/blend state, and the
/// attachment formats it was built against. Everything here is either
/// invariant across every draw using this pipeline or part of the pipeline
/// cache identity ("Normalized pipeline" in FeMeGraphicsDesign.md);
/// draw-varying state (vertex data, resources, viewport, draw counts) lives
/// in `PreparedDraw` instead.
///
/// Only the vertex/fragment conventional-pipeline shape is described so
/// far: tessellation, geometry, amplification/mesh and ray-tracing stages
/// are later milestones (G5-G8) with their own signature shapes
/// (FeMeGraphicsDesign.md's "Tessellation and geometry stage model" etc.),
/// and this skeleton has no test exercising them yet.
class GraphicsPipeline {
public:
  GraphicsPipeline(std::shared_ptr<cpu::CompiledStage> VertexStage,
                   std::shared_ptr<cpu::CompiledStage> FragmentStage,
                   PrimitiveTopology Topology, RasterState Raster,
                   DepthState Depth, BlendMode Blend, uint32_t SampleCount,
                   std::vector<AttachmentFormat> Attachments,
                   StencilState Stencil = StencilState{},
                   std::vector<BlendState> ColorBlends = {BlendState{}},
                   bool LogicOpEnable = false, LogicOp Logic = LogicOp::Copy,
                   std::array<float, 4> BlendConstants = {0.0f, 0.0f, 0.0f,
                                                          0.0f});

  const cpu::CompiledStage &getVertexStage() const { return *VertexStage; }
  const cpu::CompiledStage &getFragmentStage() const { return *FragmentStage; }
  PrimitiveTopology getTopology() const { return Topology; }
  const RasterState &getRasterState() const { return Raster; }
  const DepthState &getDepthState() const { return Depth; }
  const StencilState &getStencilState() const { return Stencil; }
  BlendMode getBlendMode() const { return Blend; }
  /// One `BlendState` per color attachment (roadmap R33's "multiple render
  /// targets"), indexed the same way `PreparedDraw::Attachments` and each
  /// fragment output `Location` are.
  llvm::ArrayRef<BlendState> getColorBlends() const { return ColorBlends; }
  bool getLogicOpEnable() const { return LogicOpEnable; }
  LogicOp getLogicOp() const { return Logic; }
  const std::array<float, 4> &getBlendConstants() const {
    return BlendConstants;
  }
  uint32_t getSampleCount() const { return SampleCount; }
  llvm::ArrayRef<AttachmentFormat> getAttachments() const {
    return Attachments;
  }

private:
  std::shared_ptr<cpu::CompiledStage> VertexStage;
  std::shared_ptr<cpu::CompiledStage> FragmentStage;
  PrimitiveTopology Topology;
  RasterState Raster;
  DepthState Depth;
  BlendMode Blend;
  uint32_t SampleCount;
  std::vector<AttachmentFormat> Attachments;
  StencilState Stencil;
  std::vector<BlendState> ColorBlends;
  bool LogicOpEnable;
  LogicOp Logic;
  std::array<float, 4> BlendConstants;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PIPELINE_H
