//===- BCDecode.h - BC1-5 (S3TC/RGTC) block decoder --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8i (deferred from H8c's own scoping pass): a real decoder for
// the five S3TC/RGTC block-compressed formats collectively named "BC1"
// through "BC5". See `feme/include/feme/Image/BCDecode.h`'s own file
// comment for the full algorithm description; as of roadmap WS1(a) that
// header holds the actual implementation (`feme::image::decodeBC*Block`),
// since none of it ever depended on `VkFormat` or any other Vulkan type.
// This header now only forwards to it, unchanged, so every existing
// caller (`BCSamplingBridge.cpp`, `BCDecodeTest.cpp`) keeps working as-is.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BCDECODE_H
#define FEME_LIB_VULKAN_BCDECODE_H

#include <cstdint>

namespace feme::vulkan {

/// Decodes one 8-byte (64-bit) BC1 block -- a `color0`/`color1` RGB565
/// pair plus a 2-bit-per-texel index -- into 16 RGBA8 texels, written
/// row-major (row 0 first, left to right) to \p Output as 4 bytes (R, G,
/// B, A) per texel; \p Output must have room for 64 bytes. Whether the
/// block is in 4-color (opaque) or 3-color-plus-black mode is
/// determined purely by comparing the block's own raw `color1`/`color0`
/// 16-bit words (`color1 > color0`, matching this project's own
/// reference, `VK-GL-CTS`'s `tcuCompressedTexture.cpp`'s
/// `decompressBc1`, exactly -- a corner case at `color0 == color1` that
/// the Khronos specification's own prose describes slightly
/// differently, see `BCDecode.cpp`'s own comment on this). When
/// \p HasAlpha is true (`VK_FORMAT_BC1_RGBA_*_BLOCK`) and the block is
/// in 3-color mode, a texel whose own index is exactly 3 decodes to
/// fully transparent (RGBA = `{0, 0, 0, 0}`); when \p HasAlpha is false
/// (`VK_FORMAT_BC1_RGB_*_BLOCK`), that same texel instead decodes to
/// opaque black (RGBA = `{0, 0, 0, 255}`). Every other texel is fully
/// opaque.
void decodeBC1Block(const uint8_t Block[8], bool HasAlpha, uint8_t *Output);

/// Decodes one 16-byte (128-bit) BC2 block -- 64 bits of explicit 4-bit-
/// per-texel alpha followed by a 64-bit BC1-shaped color block, always
/// treated as 4-color (opaque) mode regardless of the color block's own
/// `color0`/`color1` comparison -- into 16 RGBA8 texels, same row-major
/// output convention as \c decodeBC1Block; \p Output must have room for
/// 64 bytes.
void decodeBC2Block(const uint8_t Block[16], uint8_t *Output);

/// Decodes one 16-byte (128-bit) BC3 block -- 64 bits of interpolated
/// (BC4-shaped) alpha followed by a 64-bit BC1-shaped color block,
/// always treated as 4-color (opaque) mode regardless of the color
/// block's own `color0`/`color1` comparison -- into 16 RGBA8 texels,
/// same row-major output convention as \c decodeBC1Block; \p Output
/// must have room for 64 bytes.
void decodeBC3Block(const uint8_t Block[16], uint8_t *Output);

/// Decodes one 8-byte (64-bit) BC4 block -- an `endpoint0`/`endpoint1`
/// pair plus a 3-bit-per-texel index selecting one of 8 interpolated
/// (or, in the "3-bit-index, endpoint0 <= endpoint1" case, min/max)
/// values between them -- into 16 single-channel texels, written
/// row-major to \p Output (which must have room for 16 bytes). For
/// \p Signed == false, each output byte is an unsigned `[0, 255]` UNORM
/// value; for \p Signed == true, each output byte, reinterpreted as
/// `int8_t`, is a signed `[-127, 127]` SNORM value (an endpoint byte of
/// exactly -128 is not a valid encoder output per the specification,
/// but is still handled, per the specification's own explicit rule,
/// identically to -127).
void decodeBC4Block(const uint8_t Block[8], bool Signed, uint8_t *Output);

/// Decodes one 16-byte (128-bit) BC5 block -- two independent BC4-shaped
/// 64-bit sub-blocks, the first decoding to a red channel and the
/// second to a green channel -- into 16 two-channel (R, G) texels,
/// written row-major to \p Output (which must have room for 32 bytes,
/// R then G per texel), using the same UNORM/SNORM output convention as
/// \c decodeBC4Block for each channel independently.
void decodeBC5Block(const uint8_t Block[16], bool Signed, uint8_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BCDECODE_H
