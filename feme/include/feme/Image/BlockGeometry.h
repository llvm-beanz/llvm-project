//===- BlockGeometry.h - API-neutral format/block geometry -------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS0 ("Separate API-neutral format/block geometry ... from
// Format.*/Image.*", feme/docs/WARPRoadmap.md): the per-`ResourceFormat`
// element-size and block-footprint computations `feme::vulkan::Format.*`
// already implemented never actually examined a `VkFormat` or any other
// Vulkan type -- they are pure functions of `feme::cpu::ResourceFormat`
// (RuntimeABI.h), the same CPU-target format enumeration a future Direct3D
// frontend's DXGI-format mapping would also need to produce. This header is
// the first slice of the design's proposed `FeMeImage` library: it depends
// only on `feme::cpu::ResourceFormat` and LLVM support, not on Vulkan,
// Windows, or `FeMeGraphics` (see "Sharing contract" in
// feme/docs/FeMeWARPDesign.md), so a D3D frontend can link it directly
// instead of duplicating these tables.
//
// `feme::vulkan::formatElementSize`/`blockWidth`/`blockHeight`/
// `bytesPerBlock`/`isASTCSRGBFormat` (feme/lib/Vulkan/Format.h) now forward
// to this header's functions unchanged -- every existing Vulkan caller and
// advertised format capability is unaffected by this extraction.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_IMAGE_BLOCKGEOMETRY_H
#define FEME_IMAGE_BLOCKGEOMETRY_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <cstdint>

namespace feme::image {

/// The size in bytes of one texel of \p Format, as laid out in linear
/// buffer/image memory. Returns 0 for `ResourceFormat::Unknown` and for a
/// block-compressed format (see `isBlockCompressedFormat`, RuntimeABI.h):
/// such a format has no single-texel size to report, only a whole block's
/// -- use `bytesPerBlock` instead.
uint32_t formatElementSize(feme::cpu::ResourceFormat Format);

/// The width/height in texels of one addressable block of \p Format, or
/// `{1, 1}` for every non-block-compressed format (including
/// `ResourceFormat::Unknown`) -- a caller that always divides an extent by
/// these values, rather than branching on `isBlockCompressedFormat`, gets
/// the right answer either way.
uint32_t blockWidth(feme::cpu::ResourceFormat Format);
uint32_t blockHeight(feme::cpu::ResourceFormat Format);

/// The size in bytes of one whole `blockWidth(Format) x blockHeight(Format)`
/// block of \p Format. Equal to `formatElementSize(Format)` for a
/// non-block-compressed format, so the same "always call this, never
/// branch" pattern as `blockWidth`/`blockHeight` applies. Returns 0 for
/// `ResourceFormat::Unknown`.
uint32_t bytesPerBlock(feme::cpu::ResourceFormat Format);

/// Returns whether \p Format is one of the 14 `_SRGB` ASTC LDR footprints
/// rather than its `_UNORM` counterpart (roadmap L228(b)).
bool isASTCSRGBFormat(feme::cpu::ResourceFormat Format);

} // namespace feme::image

#endif // FEME_IMAGE_BLOCKGEOMETRY_H
