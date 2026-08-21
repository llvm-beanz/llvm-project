//===- Format.h - VkFormat -> feme::cpu::ResourceFormat mapping -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// V4 ("Map supported VkFormat values to ResourceFormat", see "V4: Typed
// buffers and broader compute" in feme/docs/FeMeVulkanDesign.md): a table
// from every `VkFormat` a texel buffer or (V5+) image descriptor may name to
// the corresponding `feme::cpu::ResourceFormat` the CPU target's descriptor
// heap already carries -- see `feme::cpu::ResourceFormat` in
// feme/include/feme/Target/CPU/RuntimeABI.h. Only the identity 32-bit
// formats and `R8G8B8A8_UNORM` are actually *consumed* by a texel buffer in
// this milestone (see Descriptor.h's file comment for why); this table maps
// every format `ResourceFormat` itself lists, ahead of the images work that
// will need the rest of them (V5), so it does not have to be revisited
// piecemeal later.
//
// Block-compressed formats (roadmap E20, the ASTC LDR groundwork
// "V5: Images and sampling"'s own status note found missing): a
// block-compressed `VkFormat` still maps to one `ResourceFormat` value the
// same as any other, but its bytes are addressed a whole
// `blockWidth() x blockHeight()` block at a time rather than one texel at
// a time -- `formatElementSize` (a texel's size) is meaningless for it, so
// `blockWidth`/`blockHeight`/`bytesPerBlock` exist alongside it and
// `feme::cpu::isBlockCompressedFormat` (RuntimeABI.h) distinguishes the two
// families. `Image.{h,cpp}`'s subresource layout math is written against
// these three functions rather than `formatElementSize` directly, so it
// already generalizes to whichever family `Format` belongs to.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_FORMAT_H
#define FEME_LIB_VULKAN_FORMAT_H

#include "feme/Target/CPU/RuntimeABI.h"

#include <vulkan/vulkan_core.h>

#include <optional>

namespace feme::vulkan {

/// Maps \p Format to the `feme::cpu::ResourceFormat` it corresponds to, or
/// `std::nullopt` if this ICD does not recognize/support \p Format at all.
std::optional<feme::cpu::ResourceFormat> mapVkFormat(VkFormat Format);

/// The size in bytes of one texel of \p Format, as this ICD lays it out in
/// linear buffer/image memory -- the same value `mapVkFormat`'s caller needs
/// to compute a texel buffer's element stride. Returns 0 for a format
/// `mapVkFormat` does not recognize, and for a block-compressed format (see
/// `isBlockCompressedFormat`'s comment): such a format has no single-texel
/// size to report, only a whole block's -- use `bytesPerBlock` instead.
uint32_t formatElementSize(feme::cpu::ResourceFormat Format);

/// Block-compressed formats (roadmap E20, "Block-compressed formats" below):
/// the width/height in texels of one addressable block of \p Format, or
/// `{1, 1}` for every non-block-compressed format (so a caller that always
/// divides an extent by these values, rather than branching on
/// `isBlockCompressedFormat`, gets the right answer either way).
uint32_t blockWidth(feme::cpu::ResourceFormat Format);
uint32_t blockHeight(feme::cpu::ResourceFormat Format);

/// The size in bytes of one whole `blockWidth(Format) x blockHeight(Format)`
/// block of \p Format -- the block-compressed analogue of
/// `formatElementSize`'s per-texel size, and what every ASTC footprint's
/// block collapses to regardless of its width/height (128 bits, always).
/// Equal to `formatElementSize(Format)` for a non-block-compressed format,
/// so the same "always call this, never branch" pattern as
/// `blockWidth`/`blockHeight` applies. Returns 0 for a format `mapVkFormat`
/// does not recognize.
uint32_t bytesPerBlock(feme::cpu::ResourceFormat Format);

/// Returns whether \p Format is one of the formats the CPU runtime's
/// typed-load/store helpers (feme/runtime/CPU/FeMeRuntimeCPU.c) actually
/// implement a conversion for, and so may legally back a texel buffer's
/// `VkBufferView` (see Descriptor.h's file comment). `vkCreateBufferView`
/// rejects every other format -- including one `mapVkFormat` itself maps
/// successfully -- with `VK_ERROR_FORMAT_NOT_SUPPORTED` rather than silently
/// misconverting it.
bool isTexelBufferFormatSupported(feme::cpu::ResourceFormat Format);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_FORMAT_H
