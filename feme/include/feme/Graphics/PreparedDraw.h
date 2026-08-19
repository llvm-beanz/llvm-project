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
// logic itself. Roadmap R32 ("Basic triangle pipeline") adds the executor
// (Executor.h) that consumes one.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_PREPAREDDRAW_H
#define FEME_GRAPHICS_PREPAREDDRAW_H

#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::graphics {

/// One vertex attribute a bound vertex buffer supplies: the shader-input
/// location it feeds, its storage format, and its byte offset within one
/// buffer element -- matching the scene YAML's own `vertex-buffers[].
/// attributes[]` entry shape ("Textual scene and image fixtures" in
/// feme/docs/Design.md) one-for-one, since the executor's vertex fetch
/// (roadmap R32) needs exactly this to convert bound bytes into a vertex
/// shader's input signature elements.
struct VertexAttribute {
  uint32_t Location = 0;
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Offset = 0;
};

/// One bound vertex buffer: a byte range, the stride between consecutive
/// elements, and the attributes it supplies, matching the scene YAML's own
/// `vertex-buffers` entry shape ("Textual scene and image fixtures" in
/// feme/docs/Design.md).
struct VertexBufferBinding {
  uint32_t Binding = 0;
  uint32_t Stride = 0;
  llvm::ArrayRef<uint8_t> Data;
  llvm::ArrayRef<VertexAttribute> Attributes;
  /// Whether this binding advances per vertex or per instance
  /// (`VkVertexInputRate`): a per-instance binding is indexed by the
  /// invocation's instance index rather than its vertex index.
  bool PerInstance = false;
};

/// The scalar type an index buffer's elements store.
enum class IndexType : uint8_t {
  UInt16,
  UInt32,
};

/// A bound index buffer for an indexed draw (`DrawCommand::Indexed`).
struct IndexBufferBinding {
  IndexType Type = IndexType::UInt32;
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
  /// The number of array layers `Data` holds (roadmap R34, "layered
  /// rendering" -- see `feme::graphics::resolveRenderTargetArrayLayer` in
  /// LayeredRendering.h), each `Width * Height * (bytes per pixel)` bytes,
  /// stored consecutively layer-major. `1` for a non-layered attachment
  /// (every draw before R34, and any R34 draw whose pre-raster stages
  /// write no `RenderTargetArrayIndex` output, since that output is what
  /// selects a layer other than 0).
  uint32_t ArrayLayers = 1;
};

/// The optional depth/stencil attachments a draw tests/writes against
/// (roadmap R33, "Depth, stencil, blending, and multisampling"; combined-
/// format support added by roadmap C1, "Mandatory formats"). For a pure
/// depth (`D16_UNORM`/`D32_FLOAT`) or pure stencil (`S8_UINT`) attachment,
/// `Depth` and `Stencil` are two separate single-component images. For a
/// combined format (`D24_UNORM_S8_UINT`), both are bound and share the
/// same underlying storage (one 4-byte word per texel: the low 24 bits
/// depth, the high byte stencil) -- `feme::graphics::packDepthClear`/
/// `packStencilClear` (and the executor's own `readDepth`/`writeDepth`/
/// `readStencil`/`writeStencil`) are read-modify-writes of only their own
/// half, so testing/writing one never corrupts the other. An empty `Data`
/// member means that attachment is not bound; `Pipeline`'s
/// `DepthState::TestEnable`/`WriteEnable` and `StencilState::TestEnable`
/// require the matching attachment to be bound.
struct DepthStencilAttachment {
  AttachmentView Depth;
  AttachmentView Stencil;
};

/// One non-indexed or indexed draw command, matching the scene YAML's own
/// `draws` entry shape. A non-indexed draw (`Indexed == false`) fetches
/// vertex `FirstVertex + i` for `i` in `[0, VertexCount)`; an indexed draw
/// looks `i` up through the bound index buffer at `FirstIndex + i` instead,
/// adding `VertexOffset` to the index it reads (`VertexCount` is then the
/// index count), matching Vulkan/Direct3D's own indexed-draw semantics.
struct DrawCommand {
  uint32_t VertexCount = 0;
  uint32_t InstanceCount = 1;
  uint32_t FirstVertex = 0;
  uint32_t FirstInstance = 0;
  bool Indexed = false;
  uint32_t FirstIndex = 0;
  int32_t VertexOffset = 0;
};

/// A snapshot of one draw's dynamic state: color attachments, viewport and
/// scissor, bound vertex buffers, the resource heap the vertex/fragment
/// stages read from, and the draw commands to execute against them. Owned
/// for the prepared draw's duration, exactly like `cpu::DispatchResources`
/// is owned for one dispatch's duration -- neither type copies the
/// buffers/heaps it references.
struct PreparedDraw {
  llvm::MutableArrayRef<AttachmentView> Attachments;
  DepthStencilAttachment DepthStencil;
  ViewportState Viewport;
  ScissorRect Scissor;
  llvm::ArrayRef<VertexBufferBinding> VertexBuffers;
  IndexBufferBinding IndexBuffer;
  cpu::DispatchResources Resources;
  llvm::ArrayRef<DrawCommand> Draws;
  /// When the pipeline's sample count is greater than 1, `Attachments`
  /// stores each color attachment's multisample data (samples interleaved
  /// per pixel: see Executor.cpp's `readDepth` comment for the exact
  /// layout every multisample attachment shares). `ResolveAttachments`,
  /// when non-empty, names one single-sample attachment per entry of
  /// `Attachments` (same index, same format/extent) that the executor
  /// resolves (box-filter average) into once every draw in this
  /// `PreparedDraw` completes -- matching Vulkan/Direct3D's own explicit
  /// resolve-attachment model rather than an implicit "always resolve"
  /// rule. Empty for a single-sample pipeline, which has nothing to
  /// resolve.
  llvm::MutableArrayRef<AttachmentView> ResolveAttachments;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_PREPAREDDRAW_H
