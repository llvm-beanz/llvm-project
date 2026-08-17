//===- LayeredRendering.h - Render-target array layer selection -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::resolveRenderTargetArrayLayer and
// feme::graphics::getAttachmentLayerByteOffset, roadmap R34's "layered
// rendering": routing a primitive to one array layer of a layered
// attachment (`PreparedDraw.h`'s `AttachmentView::ArrayLayers`) based on
// its last pre-raster stage's `SignatureSystemValue::RenderTargetArrayIndex`
// output (Signature.h).
//
// This is the selection/addressing logic only, exercised standalone by
// unittests/Graphics/LayeredRenderingTest.cpp; wiring a real vertex/
// geometry stage's compiled `RenderTargetArrayIndex` output through
// `feme::graphics::executeDraws` into a per-primitive call to
// `resolveRenderTargetArrayLayer` is a documented follow-up, alongside the
// other CPU-target wrapper/executor integration this milestone defers (see
// Tessellator.h/Patch.h/GeometryStream.h's own file comments).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_LAYEREDRENDERING_H
#define FEME_GRAPHICS_LAYEREDRENDERING_H

#include <cstdint>
#include <optional>

namespace feme::graphics {

/// Resolves which array layer a primitive renders into, given its
/// `RenderTargetArrayIndex` (or `ViewportArrayIndex`) system-value output
/// \p RequestedLayer and the bound attachment's `ArrayLayers` count \p
/// LayerCount, per Vulkan/Direct3D's shared rule: a negative or
/// out-of-range index discards the primitive entirely (returns
/// `std::nullopt`) rather than clamping it onto a valid layer, since
/// silently redirecting to layer 0 would misrender exactly the "plausible
/// but wrong image" this codebase's system-value handling otherwise takes
/// care to avoid (see "Unsupported system values are diagnosed ... not
/// silently replaced with zero" in FeMeGraphicsDesign.md's "Builtins and
/// system values", the same principle applied here to an out-of-range
/// value rather than an unsupported one).
std::optional<uint32_t> resolveRenderTargetArrayLayer(int32_t RequestedLayer,
                                                      uint32_t LayerCount);

/// The byte offset of layer \p Layer's first byte within a layered
/// attachment's `Data` (`PreparedDraw.h`'s `AttachmentView`), given one
/// layer's own byte size \p LayerSizeBytes (typically
/// `Width * Height * (bytes per pixel)`), matching the layer-major storage
/// `AttachmentView::ArrayLayers`'s own comment documents.
uint64_t getAttachmentLayerByteOffset(uint32_t Layer, uint64_t LayerSizeBytes);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_LAYEREDRENDERING_H
