//===- CopyWalk.h - API-neutral image-copy region walking -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS1(c) ("Move the reusable copy/resolve kernels ... out of
// CommandBuffer.cpp", feme/docs/WARPRoadmap.md -- the implementation this
// row describes actually lives in `feme::vulkan::ImageOps.cpp`'s
// `runCopyImage`, not `CommandBuffer.cpp`; the roadmap text predates that
// discovery): the row/depth/array-layer-vs-3D-depth-slice stride-walking
// geometry `runCopyImage` computes once per `VkImageCopy` region -- before
// ever touching a live `Image`'s texel/block storage -- never examines
// anything but plain scalar block shapes, sample counts, and region
// offsets/extents, so it moves here unchanged. The actual texel/block
// `memcpy` (`Image::texelPointer`/`blockPointer`) stays in
// `ImageOps.cpp`, since it genuinely needs a live `Image`'s backing
// storage; only the index arithmetic deciding *where* to copy moves.
//
// `runBlitImage`/`runResolveImage` were evaluated for the same kind of
// extraction and found to have no comparable pure "kernel" left over:
// `runBlitImage`'s per-texel loop is inseparable from its BC/ETC2/ASTC
// decode-then-resample pipeline (every loop iteration's bounds and
// sub-pixel sample positions depend on the filter and the source's
// decoded-format byte layout, not just block/sample-count geometry), and
// `runResolveImage`'s loop is already a direct, un-block-shaped nested
// `Layer`/`Z`/`Y`/`X` walk with no unit-conversion or 3D-vs-array-layer
// branch to factor out (only `runCopyImage` needs one, since only a copy
// can pair a 3D image with a differently-shaped 2D array image or mix
// block-compressed/uncompressed block shapes on its two sides). See
// `agent_thoughts.md`'s WS1(c) entry for the full evaluation.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMAGE_COPYWALK_H
#define FEME_IMAGE_COPYWALK_H

#include <cstdint>

namespace feme::image {

/// One `VkImageCopy`-style region's resolved walk geometry: how many
/// block/texel units wide and tall each of its \p SliceCount 2D slices is,
/// how many contiguous bytes one slice-row's `memcpy` moves (already
/// folded together with \p SampleCount, since every sample of one texel is
/// stored contiguously), and each side's starting offset in its own
/// block/texel units (which only differ from the other side's when the
/// two images' compressed-block shapes differ).
struct ImageCopyRegionWalk {
  uint32_t WidthUnits = 0;
  uint32_t HeightUnits = 0;
  uint32_t SliceCount = 0;
  uint64_t RowBytes = 0;
  uint32_t SrcOffsetXUnits = 0;
  uint32_t SrcOffsetYUnits = 0;
  uint32_t DstOffsetXUnits = 0;
  uint32_t DstOffsetYUnits = 0;
};

/// Computes \p RegionWidth/\p RegionHeight/\p RegionDepth (a
/// `VkImageCopy::extent`, always expressed in the *source* image's own
/// block/texel units, matching real Vulkan's own copy-region rule) and
/// \p SrcOffsetX/\p SrcOffsetY/\p DstOffsetX/\p DstOffsetY (a region's
/// `srcOffset`/`dstOffset`, each in its own image's texel units) into one
/// region's walk geometry, given each side's compressed-block shape
/// (\p SrcBlockWidth/\p SrcBlockHeight/\p DstBlockWidth/\p DstBlockHeight;
/// `1`/`1` for an uncompressed image), the common per-unit byte size
/// \p UnitSize (a copy requires both sides to share one -- see
/// `runCopyImage`'s own check), and \p SampleCount (always `1` for a
/// block-compressed image). \p SrcIs3D / \p DstIs3D / \p ResolvedSrcLayerCount
/// select \p SliceCount the same way real Vulkan's own "Image Copies" rule
/// does: a copy naming a 3D image on either side steps that side's
/// `Region*Offset.z` for \p RegionDepth slices; a copy between two
/// non-3D images instead steps both sides' array layer for
/// \p ResolvedSrcLayerCount slices (already resolved against the source
/// image's own `VK_REMAINING_ARRAY_LAYERS` sentinel handling by the
/// caller, e.g. `Image::resolvedLayerCount`).
ImageCopyRegionWalk computeImageCopyRegionWalk(
    uint32_t SrcBlockWidth, uint32_t SrcBlockHeight, uint32_t DstBlockWidth,
    uint32_t DstBlockHeight, uint32_t UnitSize, uint32_t SampleCount,
    bool SrcIs3D, bool DstIs3D, uint32_t RegionWidth, uint32_t RegionHeight,
    uint32_t RegionDepth, uint32_t SrcOffsetX, uint32_t SrcOffsetY,
    uint32_t DstOffsetX, uint32_t DstOffsetY, uint32_t ResolvedSrcLayerCount);

/// One 2D slice's source/destination array-layer and 3D-depth-slice
/// coordinates within a region's walk, for slice index \p SliceIndex of
/// \p SliceCount (see `ImageCopyRegionWalk::SliceCount`). Exactly one of
/// each side's `*Layer`/`*Z` pair varies with \p SliceIndex: whichever side
/// is 3D (\p SrcIs3D / \p DstIs3D) steps its own \p SrcOffsetZ/
/// \p DstOffsetZ; the other side instead steps its own
/// \p SrcBaseArrayLayer/\p DstBaseArrayLayer. A same-dimensionality (2D-to
/// -2D or 3D-to-3D) copy is the special case where both sides agree on
/// which kind of step applies to neither, so this same formula computes it
/// correctly too (both base values pass through unmodified, with
/// \p SliceIndex consumed by whichever side's `Is3D` flag is set).
struct ImageCopySliceLocation {
  uint32_t SrcLayer = 0;
  uint32_t SrcZ = 0;
  uint32_t DstLayer = 0;
  uint32_t DstZ = 0;
};

ImageCopySliceLocation
computeImageCopySliceLocation(bool SrcIs3D, bool DstIs3D,
                              uint32_t SrcBaseArrayLayer, uint32_t SrcOffsetZ,
                              uint32_t DstBaseArrayLayer, uint32_t DstOffsetZ,
                              uint32_t SliceIndex);

} // namespace feme::image

#endif // FEME_IMAGE_COPYWALK_H
