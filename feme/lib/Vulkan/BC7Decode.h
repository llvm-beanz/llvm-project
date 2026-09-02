//===- BC7Decode.h - BC7 (BPTC) block decoder ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H8l (split from H8k's own BC6H/BC7 scoping pass): a real decoder
// for `VK_FORMAT_BC7_{UNORM,SRGB}_BLOCK` ("BPTC", `VK_FORMAT_BC7_*`, 2 of
// the remaining 4 `VK_FORMAT_BC*` formats after H8i's own BC1-5 slice).
// This file implements the algorithm the Khronos BPTC bitstream
// specification defines
// (`https://github.com/KhronosGroup/DataFormat/blob/main/bptc.txt`) --
// the same specification (and, cross-checked directly against its own
// reference decoder while writing this, `VK-GL-CTS`'s own
// `tcuCompressedTexture.cpp`'s `decompressBc7`) a compliant
// hardware/software decoder anywhere must follow to produce the one
// correct answer for a given block; there is no FeMe-specific design
// choice here, the same precedent `ASTCDecode.h`/`ETC2Decode.h`/
// `BCDecode.h`'s own file comments already establish for this
// directory's other block-compressed formats.
//
// Unlike BC1-5's small, mostly-shared color/alpha interpolation shape,
// BC7 has 8 distinct per-block "modes" (identified by the position of
// the lowest set bit of the block's own first byte), each with its own
// bit-packing order and field widths for: how many of the block's 16
// texels are split into 1-3 "subsets" (each subset gets its own pair of
// endpoint colors); which of 64 fixed 4x4 partition patterns (looked up
// by an explicit partition-selection field, one table for 2 subsets and
// a separate table for 3 subsets) assigns each texel to a subset;
// optional per-block channel "rotation" and index-selection bits (modes
// 4 and 5 only, letting the encoder swap a color channel with alpha, or
// swap which of two index fields drives color vs. alpha interpolation,
// per block); optional per-endpoint or shared low-bit "P-bits" folded
// into each endpoint channel before 8-bit reconstruction; and a
// variable interpolation-index bit width per texel, made one bit
// narrower for exactly one "anchor" texel per subset (always index 0
// for a block's own first/only subset, or a mode-and-partition-specific
// texel a fixed lookup table names for any other subset) -- an
// encoding-symmetry trick letting the format save one bit per subset
// without losing precision, since flipping both endpoints of a subset
// and inverting every index in it produces an identical decoded result,
// so a decoder can always assume that bit is zero without ever having
// to store it. This file's own internal partition-pattern and
// anchor-index lookup tables are copied verbatim from `VK-GL-CTS`'s own
// `decompressBc7` (the actual, already-conformance-tested ground truth
// for what a real `dEQP-VK.texture.compressed_format.*` BC7 case scores
// against), and were spot-checked against the specification's own
// worked numeric example (`bptc.txt`'s own mode-2/partition-6/texel-9
// walkthrough) for independent confidence rather than trusting a single
// source.
//
// BC7's endpoint-interpolation formula itself is a genuinely different
// shape from BC1-5's plain truncating-thirds/halves arithmetic: BC7
// always interpolates via a fixed per-index-width weight table
// (`weights2`/`weights3`/`weights4`, one entry per possible 2/3/4-bit
// index value) and a rounding formula,
// `(((64 - weight) * a + weight * b + 32) >> 6)` -- consistent across
// every mode and every channel (color and alpha alike), unlike BC1-5's
// per-mode distinct formulas. There is no BC7 analogue of BC1-5's own
// documented CTS-vs-spec-prose discrepancies: this formula is identical
// between the specification text and `VK-GL-CTS`'s own reference
// decoder, so no such discrepancy needed to be resolved here.
//
// Nothing in this ICD calls `decodeBC7Block` yet: like `BCDecode.h`'s
// own H8i precedent, `vkCreateImage` still rejects
// `VK_FORMAT_BC7_{UNORM,SRGB}_BLOCK` outright, `Format.cpp` has no
// `ResourceFormat` enumerators for either format, and `textureCompressionBC`
// stays `VK_FALSE` -- this function exists as a standalone,
// directly-unit-tested (`BC7DecodeTest.cpp`) building block for the
// wiring a follow-on roadmap row adds (alongside `BCDecode.h`'s own
// BC1-5 wiring and `ETC2Decode.h`'s own H8j).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BC7DECODE_H
#define FEME_LIB_VULKAN_BC7DECODE_H

#include <cstdint>

namespace feme::vulkan {

/// Decodes one 16-byte (128-bit) BC7 block into 16 RGBA8 texels, written
/// row-major (row 0 first, left to right) to \p Output as 4 bytes (R, G,
/// B, A) per texel; \p Output must have room for 64 bytes. Selects one
/// of BC7's own 8 modes from the block's own first byte (the position
/// of its lowest set bit), decodes that mode's own partition, rotation,
/// index-selection, endpoint, and per-texel index fields per the
/// specification's own mode-dependent bit layout, and interpolates each
/// texel's final RGBA value from its subset's own two endpoint colors
/// using BC7's fixed per-index-width weight-table formula (the same
/// formula for every mode, unlike BC1-5's own per-mode-distinct
/// arithmetic). A block whose first byte is exactly zero (no mode bit
/// set at all) is not a valid encoder output per the specification, but
/// this function still does not misbehave on it: every texel decodes to
/// fully transparent black, mirroring `VK-GL-CTS`'s own reference
/// decoder's behavior for the same input.
void decodeBC7Block(const uint8_t Block[16], uint8_t *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BC7DECODE_H
