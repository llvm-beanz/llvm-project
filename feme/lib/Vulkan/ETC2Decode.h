//===- ETC2Decode.h - ETC2/EAC block decoder --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8c ("BC1-7/ETC2/EAC compressed-format sampling", the first
// slice of it): a real decoder for the two 64-bit RGB(A) ETC2 block
// shapes (`decodeETC2Block`/`decodeETC2PunchthroughAlphaBlock`, covering
// all five ETC2 modes -- `individual`, `differential`, `T`, `H`, and
// `planar`) and the 64-bit single-channel EAC block
// (`decodeEACBlock`, covering both the unsigned and signed variants),
// together spanning all 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` formats
// (`RGB8`, `RGB8_SRGB`, `RGB8A1`, `RGB8A1_SRGB`, `R11`, `R11_SNORM`,
// `RG11`, `RG11_SNORM` -- `RGBA8`/`RGBA8_SRGB` compose `decodeETC2Block`
// with two `decodeEACBlock` calls, per the specification's own "the
// alpha part is encoded separately" design). This file implements the
// algorithm the Khronos ETC1/ETC2/EAC bitstream specification defines
// (`https://github.com/KhronosGroup/DataFormat/blob/master/etc1.txt`/
// `etc2.txt`) -- the same specification a compliant hardware/software
// decoder anywhere must follow to produce the one correct answer for a
// given block, there is no FeMe-specific design choice here, the same
// precedent `ASTCDecode.h`'s own file comment already establishes for
// this directory's other block-compressed formats -- not any particular
// existing implementation's code; every bit position and formula below
// was cross-checked directly against that specification's own tables
// and worked numeric examples while writing it.
//
// Nothing in this ICD calls any of these three functions yet: like
// `ASTCDecode.h`'s own initial slice (roadmap E20), `vkCreateImage`
// still rejects every ETC2/EAC `VkFormat` outright (`Image.h`'s file
// comment), `Format.cpp` has no `ResourceFormat` enumerators for any of
// these 10 formats, and no feature bit is flipped -- these functions
// exist as standalone, directly-unit-tested (`ETC2DecodeTest.cpp`)
// building blocks for the wiring (`Format.cpp`/`CommandBuffer.cpp`/
// `ImageOps.cpp`/`PhysicalDeviceInfo.cpp`'s `textureCompressionETC2`)
// a follow-on roadmap row adds, mirroring `ASTCDecode.h`'s own E20-then-
// E22 sequencing. BC1-7 (the other half of roadmap H8c) is not attempted
// in this slice at all -- see `Roadmap.md`'s own H8c entry for why.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_ETC2DECODE_H
#define FEME_LIB_VULKAN_ETC2DECODE_H

#include <cstdint>

namespace feme::vulkan {

/// Decodes one 8-byte (64-bit) RGB ETC2 block -- `individual`,
/// `differential`, `T`, `H`, or `planar` mode, selected per the block's
/// own bit pattern per the specification's own mode-selection algorithm
/// -- into 16 RGBA8 texels, written row-major (row 0 = v=0 first, left
/// to right) to \p Output as 4 bytes (R, G, B, A, in that order) per
/// texel; \p Output must have room for 64 bytes. Alpha is always 255:
/// this is the opaque-RGB format (`VK_FORMAT_ETC2_R8G8B8_{UNORM,SRGB}
/// _BLOCK`); see `decodeETC2PunchthroughAlphaBlock` for the
/// punchthrough-alpha sibling format, and this file's own header
/// comment for how `VK_FORMAT_ETC2_R8G8B8A8_*_BLOCK`'s independent alpha
/// channel is decoded (a separate `decodeEACBlock` call, per the
/// specification's own two-64-bit-halves design for that format).
void decodeETC2Block(const uint8_t Block[8], uint8_t *Output);

/// Decodes one 8-byte (64-bit) RGB-ETC2-with-punchthrough-alpha block --
/// `differential`, `T`, `H`, or `planar` mode; this format has no
/// `individual` mode at all, its would-be differential bit is instead
/// the block's own opaque flag -- into 16 RGBA8 texels, same row-major
/// output convention as \c decodeETC2Block. A pixel decodes to fully
/// transparent (RGBA = `{0, 0, 0, 0}`) when the block's own opaque flag
/// is unset and that pixel's own 2-bit index is exactly 2 (MSB=1,
/// LSB=0, the specification's own reserved "transparent" index); every
/// other pixel is fully opaque (alpha = 255).
void decodeETC2PunchthroughAlphaBlock(const uint8_t Block[8], uint8_t *Output);

/// Decodes one 8-byte (64-bit) single-channel EAC block -- used
/// standalone for `VK_FORMAT_EAC_R11_{UNORM,SNORM}_BLOCK`, and per-
/// channel (called twice, once per 8-byte half) for
/// `VK_FORMAT_EAC_R11G11_{UNORM,SNORM}_BLOCK` -- into 16 texel values,
/// written row-major to \p Output (which must have room for 16
/// `uint16_t`s). Per the specification's own "some implementations may
/// use 16 bits of accuracy" bit-replication convention: for
/// \p Signed == false, each value is an unsigned 16-bit result in
/// `[0, 65535]` (the exact 11-bit result, `[0, 2047]`, replicated to 16
/// bits); for \p Signed == true, each value, reinterpreted as
/// `int16_t`, is a signed result in `[-32767, 32767]` (the exact 11-bit
/// two's-complement result, `[-1023, 1023]`, sign-preserving bit-
/// replicated to 16 bits per the specification's own worked example).
void decodeEACBlock(const uint8_t Block[8], bool Signed, uint16_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_ETC2DECODE_H
