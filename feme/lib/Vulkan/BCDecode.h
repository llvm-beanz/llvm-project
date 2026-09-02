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
// through "BC5" -- `decodeBC1Block` (opaque or 1-bit-punchthrough-alpha
// RGB, `VK_FORMAT_BC1_RGB(A)_{UNORM,SRGB}_BLOCK`, 4 formats),
// `decodeBC2Block`/`decodeBC3Block` (RGBA with explicit or interpolated
// alpha, `VK_FORMAT_BC{2,3}_{UNORM,SRGB}_BLOCK`, 2 formats each), and
// `decodeBC4Block`/`decodeBC5Block` (one or two independent
// interpolated-single-channel blocks, `VK_FORMAT_BC{4,5}_{UNORM,SNORM}
// _BLOCK`, 2 formats each) -- covering 12 of the 16 `VK_FORMAT_BC*`
// formats. This file implements the algorithm the Khronos S3TC/RGTC
// bitstream specification defines
// (`https://github.com/KhronosGroup/DataFormat/blob/master/s3tc.txt`/
// `rgtc.txt`) -- the same specification (and, cross-checked directly
// against its own reference decoder while writing this,
// `VK-GL-CTS`'s own `tcuCompressedTexture.cpp`) a compliant
// hardware/software decoder anywhere must follow to produce the one
// correct answer for a given block; there is no FeMe-specific design
// choice here, the same precedent `ASTCDecode.h`/`ETC2Decode.h`'s own
// file comments already establish for this directory's other
// block-compressed formats.
//
// BC6H (`VK_FORMAT_BC6H_*`, HDR half-float endpoint interpolation) and
// BC7 (`VK_FORMAT_BC7_*`, 8 modes with a large per-mode
// partition/rotation/index-selection space) -- the other 4 of the 16
// `VK_FORMAT_BC*` formats -- are dramatically more complex than BC1-5
// and are not attempted in this file at all; see `Roadmap.md`'s own H8i
// entry for the scoping rationale and the new row filed for them.
//
// Nothing in this ICD calls any of these five functions yet: like
// `ETC2Decode.h`'s own initial slice (roadmap H8c), `vkCreateImage`
// still rejects every `VK_FORMAT_BC*` outright, `Format.cpp` has no
// `ResourceFormat` enumerators for any of these 12 formats, and no
// feature bit is flipped -- these functions exist as standalone,
// directly-unit-tested (`BCDecodeTest.cpp`) building blocks for the
// wiring a follow-on roadmap row adds, mirroring `ETC2Decode.h`'s own
// H8c-then-H8j sequencing.
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
