//===- ASTCDecode.h - ASTC LDR block decoder --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap E20 ("Block-compressed image groundwork + ASTC LDR decode"): a
// real decoder for one 128-bit ASTC block -- integer-sequence (bit/trit/
// quint) decoding, every weight-grid size and color-endpoint-mode
// combination the LDR profile uses, 1-4 partitions, dual-plane weights,
// and void-extent (solid-color) blocks -- covering the 14 LDR-only block
// footprints `Format.h`'s `blockWidth`/`blockHeight` list
// (`VK_FORMAT_ASTC_{4x4,...,12x12}_UNORM/SRGB_BLOCK`). This file
// implements the algorithm the Khronos ASTC bitstream specification
// defines (the same specification a compliant hardware/software decoder
// anywhere must follow to produce the one correct answer for a given
// block -- there is no FeMe-specific design choice here, unlike most of
// this directory), not any particular existing implementation's code; its
// structure was cross-checked against Google's Apache-2.0-licensed
// `astc-codec` reference decoder for bit-exactness while writing it.
//
// Roadmap E21 (`VK_EXT_texture_compression_astc_hdr`) extends this with
// `decodeASTCBlockHDR`: the same bit-unpacking/block-mode/weight-grid
// machinery above, plus the six HDR-only color endpoint modes (2, 3, 7,
// 11, 14, 15 -- specification "HDR Endpoint Decoding") and the
// pseudo-logarithmic HDR weight-interpolation formula (specification
// "Weight Application"), producing full-precision float texels instead
// of `decodeASTCBlock`'s UNORM8 ones. A block's color endpoint mode can
// independently be LDR or HDR per partition regardless of which function
// decodes it (the specification allows mixing), so `decodeASTCBlockHDR`
// still decodes every LDR mode too, just expressed in the same "shift to
// 16 bits, interpolate, then finalize" pipeline HDR modes use, per the
// specification's own "Weight Application" text.
//
// Nothing in this ICD calls either decode function yet: `vkCreateImage`
// still rejects every ASTC `VkFormat` outright (see Image.h's file
// comment), so there is no live path from a bound `Image`'s bytes to
// either one. They exist as standalone, directly-unit-tested
// (ASTCDecodeTest.cpp) building blocks for the image-copy and
// shader-sampling rework roadmap E22 is expected to add, the same
// "object model first" sequencing V5's own image/sampler support already
// used.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_ASTCDECODE_H
#define FEME_LIB_VULKAN_ASTCDECODE_H

#include <cstdint>

namespace feme::vulkan {

/// Decodes one 16-byte (128-bit) ASTC block whose weight-grid footprint is
/// \p BlockWidth x \p BlockHeight texels into that many RGBA8 texels,
/// written row-major (row 0 first, left to right) to \p Output as 4 bytes
/// (R, G, B, A, in that order) per texel -- \p Output must have room for
/// `BlockWidth * BlockHeight * 4` bytes. \p BlockWidth/\p BlockHeight
/// should be one of the 14 footprints `feme::vulkan::blockWidth`/
/// `blockHeight` (Format.h) list for an ASTC `ResourceFormat`, but this
/// function itself does not depend on that -- it decodes the weight grid
/// the block's own bit pattern actually specifies against whatever
/// footprint the caller passes, per the ASTC specification.
///
/// A reserved/illegal bit pattern -- data no encoder would ever produce,
/// but that this function must still not crash or read out of bounds on,
/// per the ASTC specification's own "every bit pattern decodes to *some*
/// well-defined result" requirement -- decodes to opaque black
/// (`{0, 0, 0, 255}`) for every texel of the block, the same
/// implementation-defined-but-safe fallback color for every unsupported
/// input this function rejects (see this file's own comment: an HDR-only
/// color endpoint mode is one such case, since this decoder is LDR-only).
void decodeASTCBlock(const uint8_t Block[16], uint32_t BlockWidth,
                     uint32_t BlockHeight, uint8_t *Output);

/// Decodes one 16-byte (128-bit) ASTC block -- LDR or HDR, or a mix of
/// both across its own partitions -- whose weight-grid footprint is
/// \p BlockWidth x \p BlockHeight texels into that many RGBA texels,
/// written row-major to \p Output as 4 floats (R, G, B, A, in that order)
/// per texel -- \p Output must have room for
/// `BlockWidth * BlockHeight * 4` floats. An LDR channel's value is in
/// `[0, 1]`; an HDR channel's is an arbitrary non-negative float (an IEEE
/// half-precision value widened to `float`, per the specification's own
/// "vector of FP16 values" HDR result type), including `+Inf` if the
/// block's own data decodes to one.
///
/// A reserved/illegal bit pattern decodes to the specification's own HDR
/// error color, opaque fully-saturated magenta (`{1, 0, 1, 1}`), for
/// every texel of the block -- see `decodeASTCBlock`'s comment for the
/// same reasoning applied to this function's own error case.
void decodeASTCBlockHDR(const uint8_t Block[16], uint32_t BlockWidth,
                        uint32_t BlockHeight, float *Output);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_ASTCDECODE_H
