//===- LayeredRendering.h - Render-target array layer selection -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the small indexed-selection helpers roadmap H3's layered
// rendering and multi-viewport work share:
// feme::graphics::resolveRenderTargetArrayLayer,
// feme::graphics::resolveViewportArrayIndex, and
// feme::graphics::getAttachmentLayerByteOffset.
//
// This is the selection/addressing logic only, exercised standalone by
// unittests/Graphics/LayeredRenderingTest.cpp; wiring a real vertex/
// geometry stage's compiled `RenderTargetArrayIndex`/`ViewportArrayIndex`
// output through `feme::graphics::executeDraws` into per-primitive calls to
// these helpers was the documented H3 follow-up to R34.
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

/// Resolves which viewport/scissor array element a primitive renders through,
/// given its `ViewportArrayIndex` output \p RequestedViewport and the bound
/// viewport/scissor array length \p ViewportCount. The rule matches
/// `resolveRenderTargetArrayLayer`: negative or out-of-range discards the
/// primitive rather than silently redirecting it to viewport 0.
std::optional<uint32_t> resolveViewportArrayIndex(int32_t RequestedViewport,
                                                  uint32_t ViewportCount);

/// The byte offset of layer \p Layer's first byte within a layered
/// attachment's `Data` (`PreparedDraw.h`'s `AttachmentView`), given one
/// layer's own byte size \p LayerSizeBytes (typically
/// `Width * Height * (bytes per pixel)`), matching the layer-major storage
/// `AttachmentView::ArrayLayers`'s own comment documents.
uint64_t getAttachmentLayerByteOffset(uint32_t Layer, uint64_t LayerSizeBytes);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_LAYEREDRENDERING_H
