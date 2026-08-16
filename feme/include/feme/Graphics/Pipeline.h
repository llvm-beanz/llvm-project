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
/// Only `Replace` (no blending, matching the scene YAML's own `blend:
/// replace` spelling -- see "Textual scene and image fixtures" in
/// feme/docs/Design.md) has an executor yet; full blend-factor combinations
/// are roadmap R33's "Depth, stencil, blending, and multisampling".
enum class BlendMode : uint8_t {
  Replace,
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
                   std::vector<AttachmentFormat> Attachments);

  const cpu::CompiledStage &getVertexStage() const { return *VertexStage; }
  const cpu::CompiledStage &getFragmentStage() const { return *FragmentStage; }
  PrimitiveTopology getTopology() const { return Topology; }
  const RasterState &getRasterState() const { return Raster; }
  const DepthState &getDepthState() const { return Depth; }
  BlendMode getBlendMode() const { return Blend; }
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
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PIPELINE_H
