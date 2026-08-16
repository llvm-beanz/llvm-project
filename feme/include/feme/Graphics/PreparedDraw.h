//===- PreparedDraw.h - FeMe software graphics executor draw state -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::PreparedDraw, the dynamic per-draw
// state counterpart to Pipeline.h's `GraphicsPipeline` (roadmap R31,
// "FeMeGraphics skeleton" -- see feme/docs/Roadmap.md and the "Normalized
// pipeline" section of feme/docs/FeMeGraphicsDesign.md): "Dynamic API state
// is supplied in a PreparedDraw. The frontend validates and normalizes it
// before execution."
//
// Like `GraphicsPipeline`, this is a *description*: it snapshots the
// resources, vertex data, attachments and draw counts one draw needs, owned
// for its duration ("Threading and Lifetime Rules" in
// feme/docs/FeMeGraphicsDesign.md), but implements no fetch/assembly/raster
// logic itself. Roadmap R32 adds the executor that consumes one.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PREPAREDDRAW_H
#define FEME_GRAPHICS_PREPAREDDRAW_H

#include "feme/Target/CPU/ResourceHeap.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::graphics {

/// One bound vertex buffer: a byte range plus the stride between
/// consecutive elements, matching the scene YAML's own `vertex-buffers`
/// entry shape ("Textual scene and image fixtures" in
/// feme/docs/Design.md).
struct VertexBufferBinding {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
  llvm::ArrayRef<uint8_t> Data;
};

/// A normalized viewport: pixel-space rectangle plus the depth range it
/// maps the clip-space [-1, 1] Z range onto.
struct ViewportState {
  float X = 0.0f;
  float Y = 0.0f;
  float Width = 0.0f;
  float Height = 0.0f;
  float MinDepth = 0.0f;
  float MaxDepth = 1.0f;
};

/// A pixel-space scissor rectangle.
struct ScissorRect {
  int32_t X = 0;
  int32_t Y = 0;
  uint32_t Width = 0;
  uint32_t Height = 0;
};

/// One color attachment the draw renders into: the backing storage plus its
/// extent/format identity (`cpu::ResourceFormat`/width/height must agree
/// with the owning `GraphicsPipeline`'s own `AttachmentFormat`, checked at
/// prepare time once the executor lands in roadmap R32).
struct AttachmentView {
  llvm::MutableArrayRef<uint8_t> Data;
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Width = 0;
  uint32_t Height = 0;
};

/// One non-indexed or indexed draw command: vertex/instance counts plus
/// their starting offsets, matching the scene YAML's own `draws` entry
/// shape.
struct DrawCommand {
  uint32_t VertexCount = 0;
  uint32_t InstanceCount = 1;
  uint32_t FirstVertex = 0;
  uint32_t FirstInstance = 0;
};

/// A snapshot of one draw's dynamic state: color attachments, viewport and
/// scissor, bound vertex buffers, the resource heap the vertex/fragment
/// stages read from, and the draw commands to execute against them. Owned
/// for the prepared draw's duration, exactly like `cpu::DispatchResources`
/// is owned for one dispatch's duration -- neither type copies the
/// buffers/heaps it references.
struct PreparedDraw {
  llvm::MutableArrayRef<AttachmentView> Attachments;
  ViewportState Viewport;
  ScissorRect Scissor;
  llvm::ArrayRef<VertexBufferBinding> VertexBuffers;
  cpu::DispatchResources Resources;
  llvm::ArrayRef<DrawCommand> Draws;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PREPAREDDRAW_H
