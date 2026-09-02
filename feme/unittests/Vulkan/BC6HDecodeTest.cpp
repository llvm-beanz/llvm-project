//===- BC6HDecodeTest.cpp - BC6H (BPTC HDR) block decoder tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test blocks below were not hand-derived bit-by-bit -- BC6H's 14 modes
// each have their own irregular, hand-tuned bit layout (more so even
// than BC7's own 8), which makes that approach extremely error-prone.
// Instead, each block below was randomly generated with its first byte's
// mode-selecting bits forced to a specific mode, and its expected output
// independently computed by a from-scratch Python re-transcription of
// this file's own algorithm (and, transitively, the VK-GL-CTS reference
// decoder both were copied from) -- see roadmap H8m's own
// `agent_thoughts.md` entry. Hundreds of additional random blocks per
// mode (covering every one of the 14 modes, both signed and unsigned,
// plus all 4 reserved/invalid first-byte patterns) were cross-checked
// the same way during development; the six covered here plus the
// invalid-mode case were chosen as a representative, directly-readable
// subset: one 2-subset delta mode (2), the one 2-subset direct mode (9),
// the one 1-subset direct mode (10), a normal 1-subset delta mode (11),
// and both of the modes with a reversed-bit-order "extra precision"
// field (12 and 13).
//
//===----------------------------------------------------------------------===//

#include "BC6HDecode.h"

#include "gtest/gtest.h"

#include <cstdint>

using namespace feme::vulkan;

namespace {

void expectTexel(const uint16_t Output[48], unsigned X, unsigned Y,
                  uint16_t R, uint16_t G, uint16_t B) {
  const uint16_t *T = Output + (Y * 4 + X) * 3;
  EXPECT_EQ(T[0], R) << "R at (" << X << "," << Y << ")";
  EXPECT_EQ(T[1], G) << "G at (" << X << "," << Y << ")";
  EXPECT_EQ(T[2], B) << "B at (" << X << "," << Y << ")";
}

// Mode 2: a 2-subset "partitioned" mode storing its 3 non-anchor
// endpoints as small signed deltas from the first, unsigned format.
TEST(BC6HDecodeTest, Mode2TwoSubsetDeltaUnsigned) {
  const uint8_t Block[16] = {0x22, 0xc7, 0x50, 0xe2, 0xbd, 0xb5, 0x04, 0x3e,
                              0x02, 0x12, 0x27, 0x4e, 0x83, 0xb4, 0xb9, 0xaf};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/false, Output);
  expectTexel(Output, 0, 0, 0x6067, 0x09d2, 0x6b96);
  expectTexel(Output, 1, 0, 0x6054, 0x09dd, 0x6b8d);
  expectTexel(Output, 2, 0, 0x608a, 0x096a, 0x6bae);
  expectTexel(Output, 3, 0, 0x60b9, 0x09c7, 0x6b51);
  expectTexel(Output, 0, 1, 0x602a, 0x09f4, 0x6b7b);
  expectTexel(Output, 1, 1, 0x6003, 0x0a0a, 0x6b69);
  expectTexel(Output, 2, 1, 0x607b, 0x09c7, 0x6b9f);
  expectTexel(Output, 3, 1, 0x60a5, 0x09a0, 0x6b78);
  expectTexel(Output, 0, 2, 0x602a, 0x09f4, 0x6b7b);
  expectTexel(Output, 1, 2, 0x6003, 0x0a0a, 0x6b69);
  expectTexel(Output, 2, 2, 0x6003, 0x0a0a, 0x6b69);
  expectTexel(Output, 3, 2, 0x60a5, 0x09a0, 0x6b78);
  expectTexel(Output, 0, 3, 0x6040, 0x09e8, 0x6b85);
  expectTexel(Output, 1, 3, 0x5fef, 0x0a14, 0x6b61);
  expectTexel(Output, 2, 3, 0x6040, 0x09e8, 0x6b85);
  expectTexel(Output, 3, 3, 0x6016, 0x09fe, 0x6b72);
}

// Mode 9: the one 2-subset mode storing all 4 endpoints in full (no
// delta transform), signed format.
TEST(BC6HDecodeTest, Mode9TwoSubsetDirectSigned) {
  const uint8_t Block[16] = {0xbe, 0x5d, 0xc8, 0xd7, 0xe1, 0x67, 0xe6, 0xb2,
                              0xc7, 0xbc, 0x51, 0x7c, 0xd5, 0x31, 0x23, 0x11};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/true, Output);
  expectTexel(Output, 0, 0, 0xcb90, 0x3ff0, 0xd350);
  expectTexel(Output, 1, 0, 0xa1c9, 0x939e, 0xe406);
  expectTexel(Output, 2, 0, 0xcb90, 0x3ff0, 0xd350);
  expectTexel(Output, 3, 0, 0x9170, 0xb450, 0xea90);
  expectTexel(Output, 0, 1, 0xb30a, 0x0ee5, 0xdd1f);
  expectTexel(Output, 1, 1, 0xa1c9, 0x939e, 0xe406);
  expectTexel(Output, 2, 1, 0xbb37, 0x1f3e, 0xd9da);
  expectTexel(Output, 3, 1, 0x9170, 0xb450, 0xea90);
  expectTexel(Output, 0, 2, 0xf250, 0x4b90, 0x8d90);
  expectTexel(Output, 1, 2, 0xce59, 0x2454, 0x0e3a);
  expectTexel(Output, 2, 2, 0xa90d, 0x845c, 0x2b0c);
  expectTexel(Output, 3, 2, 0xf250, 0x4b90, 0x8d90);
  expectTexel(Output, 0, 3, 0xe653, 0x3e7c, 0x844c);
  expectTexel(Output, 1, 3, 0xe653, 0x3e7c, 0x844c);
  expectTexel(Output, 2, 3, 0xda56, 0x3168, 0x04f7);
  expectTexel(Output, 3, 3, 0xf250, 0x4b90, 0x8d90);
}

// Mode 10: the one 1-subset mode storing its single endpoint pair in
// full (no delta transform), unsigned format.
TEST(BC6HDecodeTest, Mode10OneSubsetDirectUnsigned) {
  const uint8_t Block[16] = {0x83, 0x82, 0xd3, 0xdf, 0x0a, 0x81, 0x80, 0xa7,
                              0xf4, 0xdb, 0xf9, 0xf4, 0x5c, 0x82, 0x5e, 0xd7};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/false, Output);
  expectTexel(Output, 0, 0, 0x02b4, 0x616d, 0x2bf5);
  expectTexel(Output, 1, 0, 0x040e, 0x008b, 0x28a0);
  expectTexel(Output, 2, 0, 0x03a3, 0x1e7d, 0x29a8);
  expectTexel(Output, 3, 0, 0x03d5, 0x1066, 0x292c);
  expectTexel(Output, 0, 1, 0x036a, 0x2e58, 0x2a33);
  expectTexel(Output, 1, 1, 0x040e, 0x008b, 0x28a0);
  expectTexel(Output, 2, 1, 0x02e6, 0x5356, 0x2b79);
  expectTexel(Output, 3, 1, 0x040e, 0x008b, 0x28a0);
  expectTexel(Output, 0, 2, 0x03bc, 0x1771, 0x296a);
  expectTexel(Output, 1, 2, 0x02ff, 0x4c4a, 0x2b3b);
  expectTexel(Output, 2, 2, 0x02b4, 0x616d, 0x2bf5);
  expectTexel(Output, 3, 2, 0x0351, 0x3564, 0x2a71);
  expectTexel(Output, 0, 3, 0x03f5, 0x0797, 0x28de);
  expectTexel(Output, 1, 3, 0x02ff, 0x4c4a, 0x2b3b);
  expectTexel(Output, 2, 3, 0x0338, 0x3c6f, 0x2aaf);
  expectTexel(Output, 3, 3, 0x03d5, 0x1066, 0x292c);
}

// Mode 11: a 1-subset mode still using the delta transform (the other
// 1-subset mode, 10, is the "direct" exception), signed format.
TEST(BC6HDecodeTest, Mode11OneSubsetDeltaSigned) {
  const uint8_t Block[16] = {0xa7, 0x78, 0xdc, 0x4b, 0x5d, 0x41, 0x1e, 0x51,
                              0x90, 0xee, 0x96, 0x75, 0xfa, 0xf2, 0xdf, 0x9a};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/true, Output);
  expectTexel(Output, 0, 0, 0x74ea, 0x7357, 0x520a);
  expectTexel(Output, 1, 0, 0x7801, 0x8e94, 0x5db0);
  expectTexel(Output, 2, 0, 0x79cc, 0xd9cc, 0x646e);
  expectTexel(Output, 3, 0, 0x79cc, 0xd9cc, 0x646e);
  expectTexel(Output, 0, 1, 0x7708, 0x1a72, 0x5a02);
  expectTexel(Output, 1, 1, 0x7801, 0x8e94, 0x5db0);
  expectTexel(Output, 2, 1, 0x76a0, 0x2b8b, 0x587a);
  expectTexel(Output, 3, 1, 0x775b, 0x0cc5, 0x5b3c);
  expectTexel(Output, 0, 2, 0x786a, 0x9fac, 0x5f38);
  expectTexel(Output, 1, 2, 0x7a1f, 0xe779, 0x65a8);
  expectTexel(Output, 2, 2, 0x75a6, 0x5492, 0x54cc);
  expectTexel(Output, 3, 2, 0x7a1f, 0xe779, 0x65a8);
  expectTexel(Output, 0, 3, 0x7a1f, 0xe779, 0x65a8);
  expectTexel(Output, 1, 3, 0x7964, 0xc8b3, 0x62e6);
  expectTexel(Output, 2, 3, 0x786a, 0x9fac, 0x5f38);
  expectTexel(Output, 3, 3, 0x7801, 0x8e94, 0x5db0);
}

// Mode 12: one of the two modes storing a couple of "extra precision"
// endpoint bits in a deliberately bit-reversed field, exercising
// `getBits128`'s reversed-range path. Unsigned format.
TEST(BC6HDecodeTest, Mode12ReversedBitsUnsigned) {
  const uint8_t Block[16] = {0x8b, 0xe0, 0x56, 0xa7, 0xc6, 0xd1, 0xc8, 0xea,
                              0x37, 0x7b, 0x0d, 0x1d, 0x3e, 0xda, 0x5d, 0x46};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/false, Output);
  expectTexel(Output, 0, 0, 0x36bb, 0x342f, 0x7683);
  expectTexel(Output, 1, 0, 0x36bb, 0x342f, 0x7683);
  expectTexel(Output, 2, 0, 0x37a1, 0x354f, 0x75d2);
  expectTexel(Output, 3, 0, 0x372e, 0x34be, 0x762b);
  expectTexel(Output, 0, 1, 0x37d7, 0x3593, 0x75a8);
  expectTexel(Output, 1, 1, 0x3662, 0x33c0, 0x76c7);
  expectTexel(Output, 2, 1, 0x37d7, 0x3593, 0x75a8);
  expectTexel(Output, 3, 1, 0x367e, 0x33e2, 0x76b2);
  expectTexel(Output, 0, 2, 0x37f9, 0x35bd, 0x758e);
  expectTexel(Output, 1, 2, 0x36bb, 0x342f, 0x7683);
  expectTexel(Output, 2, 2, 0x3786, 0x352d, 0x75e7);
  expectTexel(Output, 3, 2, 0x37d7, 0x3593, 0x75a8);
  expectTexel(Output, 0, 3, 0x37d7, 0x3593, 0x75a8);
  expectTexel(Output, 1, 3, 0x36f1, 0x3472, 0x7659);
  expectTexel(Output, 2, 3, 0x3713, 0x349d, 0x763f);
  expectTexel(Output, 3, 3, 0x36d6, 0x3450, 0x766e);
}

// Mode 13: the other reversed-bit-field mode, signed format.
TEST(BC6HDecodeTest, Mode13ReversedBitsSigned) {
  const uint8_t Block[16] = {0xcf, 0x83, 0x81, 0x81, 0x4d, 0x22, 0xf9, 0x92,
                              0xca, 0xdd, 0x1e, 0x4b, 0x53, 0x1a, 0xdf, 0xe4};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/true, Output);
  expectTexel(Output, 0, 0, 0x1f1b, 0x3d08, 0x486b);
  expectTexel(Output, 1, 0, 0x1f17, 0x3d05, 0x486d);
  expectTexel(Output, 2, 0, 0x1f17, 0x3d05, 0x486d);
  expectTexel(Output, 3, 0, 0x1f17, 0x3d05, 0x486d);
  expectTexel(Output, 0, 1, 0x1f16, 0x3d04, 0x486e);
  expectTexel(Output, 1, 1, 0x1f1d, 0x3d0a, 0x486a);
  expectTexel(Output, 2, 1, 0x1f18, 0x3d06, 0x486d);
  expectTexel(Output, 3, 1, 0x1f1b, 0x3d08, 0x486a);
  expectTexel(Output, 0, 2, 0x1f1c, 0x3d09, 0x486a);
  expectTexel(Output, 1, 2, 0x1f1b, 0x3d08, 0x486b);
  expectTexel(Output, 2, 2, 0x1f18, 0x3d06, 0x486c);
  expectTexel(Output, 3, 2, 0x1f1d, 0x3d0a, 0x486a);
  expectTexel(Output, 0, 3, 0x1f16, 0x3d04, 0x486e);
  expectTexel(Output, 1, 3, 0x1f17, 0x3d05, 0x486d);
  expectTexel(Output, 2, 3, 0x1f1b, 0x3d08, 0x486a);
  expectTexel(Output, 3, 3, 0x1f16, 0x3d04, 0x486e);
}

// A block whose first byte's low 5 bits select one of BC6H's own 4
// reserved (unassigned) mode encodings is not a valid encoder output,
// but still decodes deterministically: every texel is RGB (0, 0, 0),
// mirroring VK-GL-CTS's own reference decoder's behavior for the same
// input.
TEST(BC6HDecodeTest, InvalidModeDecodesToZero) {
  const uint8_t Block[16] = {0x1f, 0x06, 0xf0, 0x39, 0xc9, 0x48, 0x16, 0x47,
                              0x39, 0x76, 0x47, 0x4b, 0x10, 0x1f, 0x45, 0x76};
  uint16_t Output[48];
  decodeBC6HBlock(Block, /*Signed=*/false, Output);
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 0; X != 4; ++X)
      expectTexel(Output, X, Y, 0, 0, 0);
}

} // namespace
