//===- SubresourceLayout.h - API-neutral image subresource layout -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS0(b): the packed, mip-major subresource-layout math
// `feme::vulkan::Image` uses (originally `Image.cpp`'s anonymous-namespace
// `computeSubresourceLayouts`/`computeSubresourceLayout`) never actually
// examined a `VkImage*` type beyond `VkImageType`'s own 1D/2D/3D
// distinction, which is captured here as a plain `Is3D` bool instead so
// this is usable without any Vulkan headers at all -- see WS0(a)'s
// `feme/Image/BlockGeometry.h` for the matching precedent of extracting an
// already-API-neutral Vulkan-file helper into `FeMeImage`.
//
// `feme::vulkan::Image.cpp` keeps its own `VkImageType`-accepting
// overloads as the call site that converts `Type == VK_IMAGE_TYPE_3D` to
// `Is3D` and maps the result back to `vulkan::ImageSubresourceLayout`
// (`VkDeviceSize`-typed, for `vkGetImageSubresourceLayout`'s own result
// shape) -- see that file for the Vulkan-facing wrapper.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_INCLUDE_FEME_IMAGE_SUBRESOURCELAYOUT_H
#define FEME_INCLUDE_FEME_IMAGE_SUBRESOURCELAYOUT_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace feme::image {

/// The API-neutral byte layout of one `(MipLevel, ArrayLayer)` subresource
/// -- see `feme::vulkan::ImageSubresourceLayout`'s own comment (Image.h)
/// for the `Size`/`ArrayPitch`/`DepthPitch` rules this implements: `Size`
/// is one array layer's own byte range for a 2D(-array) image, but the
/// *whole* depth range for a 3D image (a 3D image has exactly one array
/// layer covering every depth slice).
struct SubresourceLayout {
  uint64_t Offset;
  uint64_t Size;
  uint64_t RowPitch;
  uint64_t ArrayPitch;
  uint64_t DepthPitch;
};

/// Computes a packed, mip-major subresource layout table for an image with
/// the given extent/mip/array counts, \p SampleCount and \p
/// BlockWidth/\p BlockHeight/\p BytesPerBlock (roadmap E20; `{1, 1,
/// formatElementSize(Format)}` for a non-block-compressed format, so this
/// generalizes the original per-texel math rather than replacing it -- see
/// `BlockGeometry.h`'s file comment). \p Is3D selects between a 3D image's
/// depth-halving-per-mip rule and every other dimension's constant
/// `ArrayLayers` slice count -- real tiling is never distinguished (no
/// real hardware tiling exists to model; see `feme::vulkan::Image.h`'s own
/// file comment). Mip level `L`'s slice count is `max(1, Depth >> L)` for
/// a 3D image or `ArrayLayers` otherwise. A row is `ceil(Width /
/// BlockWidth)` blocks wide, `BytesPerBlock` bytes each; `ceil` rather
/// than plain division because only the whole *block* grid need be
/// stored, so a mip level whose texel extent isn't itself a multiple of
/// the block size still occupies one full row/column of blocks at its
/// edge. Every sample of one non-block-compressed texel is stored
/// contiguously (`SampleStride == BytesPerBlock`), so a texel's
/// `SampleCount` samples occupy `SampleCount * BytesPerBlock` bytes and a
/// row is `Width` texels wide of that; a block-compressed format is never
/// multisampled, so `SampleCount` is always 1 for one.
std::pair<std::vector<feme::cpu::FemeImageSubresourceLayout>, uint64_t>
computeSubresourceLayouts(bool Is3D, uint32_t Width, uint32_t Height,
                          uint32_t Depth, uint32_t MipLevels,
                          uint32_t ArrayLayers, uint32_t SampleCount,
                          uint32_t BlockWidth, uint32_t BlockHeight,
                          uint32_t BytesPerBlock);

/// Derives one `(MipLevel, ArrayLayer)` subresource's layout from mip
/// level `MipLevel`'s already-computed table entry \p L -- shared by a
/// live, bound image (whose table is already computed) and an info-only
/// query (whose table is computed on the fly) use. See `SubresourceLayout`'s
/// own comment for the `Size`/`ArrayPitch`/`DepthPitch` rules this
/// implements.
SubresourceLayout
computeSubresourceLayout(bool Is3D, uint32_t Depth,
                         const feme::cpu::FemeImageSubresourceLayout &L,
                         uint32_t MipLevel, uint32_t ArrayLayer);

} // namespace feme::image

#endif // FEME_INCLUDE_FEME_IMAGE_SUBRESOURCELAYOUT_H
