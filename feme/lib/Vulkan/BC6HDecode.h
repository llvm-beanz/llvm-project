//===- BC6HDecode.h - BC6H (BPTC HDR) block decoder ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8m (split from H8k's own BC6H/BC7 scoping pass, attempted after
// H8l/BC7): a real decoder for `VK_FORMAT_BC6H_{U,S}FLOAT_BLOCK` ("BPTC
// HDR", the last 2 of the 16 `VK_FORMAT_BC*` formats after H8i's BC1-5 and
// H8l's BC7). This file implements the algorithm the Khronos BPTC
// bitstream specification defines
// (`https://github.com/KhronosGroup/DataFormat/blob/main/bptc.txt`, the
// same single file covering both BC6H and BC7) -- cross-checked directly
// against its own reference decoder while writing this, `VK-GL-CTS`'s own
// `tcuCompressedTexture.cpp`'s `decompressBc6H`; there is no FeMe-specific
// design choice here, the same precedent `ASTCDecode.h`/`ETC2Decode.h`/
// `BCDecode.h`/`BC7Decode.h`'s own file comments already establish for
// this directory's other block-compressed formats.
//
// BC6H reuses BC7's own 2-subset partition/anchor-index machinery
// (`BCPartitionTables.h`'s `bcpartitions::Partitions2`/
// `AnchorSecondSubset2` -- BC6H has no 3-subset modes at all, so BC7's
// own `kPartitions3` has no BC6H counterpart), but is a fundamentally
// different, HDR-oriented format: BC6H has 14 modes (not BC7's 8),
// selected by the low 2 or 5 bits of the block's own first two bytes,
// each either a 2-subset ("partitioned") mode (10 of the 14) or a
// single-subset ("direct") mode (the remaining 4); every mode's own
// 3 (R, G, B) endpoint channels are 10-16 bit *signed or unsigned
// integers representing a scaled half-float mantissa+exponent
// combination* (per `VK_FORMAT_BC6H_{S,U}FLOAT_BLOCK`), not BC7's 8-bit
// fixed-point RGBA -- so every mode also needs its own per-channel
// "delta" bit width (nearly every 2-subset mode stores only its first
// endpoint pair in full and the remaining 1-3 endpoints as small signed
// offsets from it, added and masked back into the full endpoint's own
// bit width; two "direct" modes, one per subset count, skip this delta
// step and store every endpoint in full instead), a final per-format
// (signed/unsigned) "unquantize" step converting the stored bit pattern
// into a true half-float numeric range, and -- unique to BC6H among
// every format in this file's own family -- two of its 14 modes store
// a handful of "extra precision" bits for one endpoint's own top bits
// in a deliberately bit-reversed order (mirrored relative to every
// other multi-bit field in this format), which this file's own
// `getBits128` supports as an explicit second mode, matching
// `VK-GL-CTS`'s own reference decoder's identical behavior.
//
// Given the size and mode-specific irregularity of BC6H's own per-mode
// bit-layout table (14 modes, each a different, hand-tuned packing of
// up to 3 endpoints' worth of R/G/B/delta fields interleaved with
// several out-of-order "extra precision" bits each), this
// implementation's own per-mode field-extraction logic is ported
// directly from `VK-GL-CTS`'s own `decompressBc6H` bit-for-bit (not
// re-derived independently from `bptc.txt`'s own prose or bit-layout
// tables), the same reasoning `BC7Decode.h`'s own file comment already
// gives for copying that format's partition/anchor-index lookup tables
// verbatim: this is enumerated, per-mode bit-position data with no
// formula to derive or verify, and `VK-GL-CTS` is the actual
// conformance ground truth a real `dEQP-VK.texture.compressed_format.*`
// BC6H case scores against.
//
// Nothing in this ICD calls `decodeBC6HBlock` yet: like `BC7Decode.h`'s
// own H8l precedent, `vkCreateImage` still rejects
// `VK_FORMAT_BC6H_{U,S}FLOAT_BLOCK` outright, `Format.cpp` has no
// `ResourceFormat` enumerators for either format, and
// `textureCompressionBC` stays `VK_FALSE` -- this function exists as a
// standalone, directly-unit-tested (`BC6HDecodeTest.cpp`) building block
// for the wiring a follow-on roadmap row adds (alongside `BCDecode.h`'s
// own BC1-5 wiring, `BC7Decode.h`'s own BC7 wiring, and `ETC2Decode.h`'s
// own H8j).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BC6HDECODE_H
#define FEME_LIB_VULKAN_BC6HDECODE_H

#include <cstdint>

namespace feme::vulkan {

/// Decodes one 16-byte (128-bit) BC6H block into 16 RGB texels, written
/// row-major (row 0 first, left to right) to \p Output as 3
/// little-endian `uint16_t` values (R, G, B; BC6H has no alpha channel)
/// per texel -- each an IEEE-754 binary16 ("half float") bit pattern,
/// matching a `VK_FORMAT_R16G16B16_SFLOAT`-shaped destination -- so \p
/// Output must have room for 48 `uint16_t` (96 bytes). \p Signed selects
/// which of BC6H's two sibling formats the block was encoded as
/// (`VK_FORMAT_BC6H_SFLOAT_BLOCK` if true, `VK_FORMAT_BC6H_UFLOAT_BLOCK`
/// if false), changing both the endpoint sign-extension/unquantization
/// arithmetic and the resulting half floats' own ability to represent a
/// negative value. Selects one of BC6H's own 14 modes from the low bits
/// of the block's own first two bytes, decodes that mode's own
/// partition, endpoint (direct or delta-encoded), and per-texel index
/// fields per the specification's own mode-dependent bit layout, and
/// interpolates each texel's final RGB value from its subset's own two
/// endpoint colors using the same weighted-rounding formula
/// `BC7Decode.h`'s own interpolation uses, followed by BC6H's own
/// final unquantization step that maps the intermediate integer range
/// down to a true half-float numeric range. A block whose first byte's
/// low 5 bits select one of the format's own reserved (unassigned) mode
/// encodings is not a valid encoder output per the specification, but
/// this function still does not misbehave on it: every texel decodes to
/// RGB `(0, 0, 0)`, mirroring `VK-GL-CTS`'s own reference decoder's
/// behavior for the same input.
void decodeBC6HBlock(const uint8_t Block[16], bool Signed, uint16_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BC6HDECODE_H
