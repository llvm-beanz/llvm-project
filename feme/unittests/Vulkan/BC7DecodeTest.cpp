//===- BC7DecodeTest.cpp - BC7 (BPTC) block decoder tests ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test blocks below were not hand-derived bit-by-bit (BC7's variable,
// mode-dependent field layout makes that extremely error-prone). Instead,
// each block was constructed and its expected output independently computed
// by a small Python simulation that mirrors this file's own decode
// algorithm (and, transitively, the VK-GL-CTS reference decoder both were
// copied from) -- see roadmap H8l's own `agent_thoughts.md` entry for the
// simulation itself. This mirrors the approach that caught test-authoring
// bugs (not implementation bugs) in the BC1-5/roadmap-H8i tests.
//
//===----------------------------------------------------------------------===//

#include "BC7Decode.h"

#include "gtest/gtest.h"

#include <cstdint>

using namespace feme::vulkan;

namespace {

struct RGBA {
  uint8_t R, G, B, A;
};

RGBA texelAt(const uint8_t Output[64], unsigned X, unsigned Y) {
  const uint8_t *T = Output + (Y * 4 + X) * 4;
  return {T[0], T[1], T[2], T[3]};
}

void expectRGBA(const uint8_t Output[64], unsigned X, unsigned Y, uint8_t R,
                uint8_t G, uint8_t B, uint8_t A) {
  RGBA T = texelAt(Output, X, Y);
  EXPECT_EQ(T.R, R) << "at (" << X << "," << Y << ")";
  EXPECT_EQ(T.G, G) << "at (" << X << "," << Y << ")";
  EXPECT_EQ(T.B, B) << "at (" << X << "," << Y << ")";
  EXPECT_EQ(T.A, A) << "at (" << X << "," << Y << ")";
}

// Mode 6: the simplest mode (1 subset, no partition, no rotation, no
// index-selection), with a unique P-bit per endpoint and an explicit
// alpha channel. Exercises the plain per-endpoint P-bit fold and the
// 4-bit interpolation-index path (with the anchor's own 1-bit saving on
// texel 0).
TEST(BC7DecodeTest, Mode6OneSubsetWithAlpha) {
  const uint8_t Block[16] = {0x40, 0x19, 0x99, 0x02, 0x55, 0xF0, 0x90, 0xA8,
                              0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
  uint8_t Output[64];
  decodeBC7Block(Block, Output);
  expectRGBA(Output, 0, 0, 101, 41, 21, 145);
  expectRGBA(Output, 1, 0, 107, 48, 27, 141);
  expectRGBA(Output, 2, 0, 115, 58, 35, 136);
  expectRGBA(Output, 3, 0, 121, 65, 41, 132);
  expectRGBA(Output, 0, 1, 127, 73, 47, 128);
  expectRGBA(Output, 1, 1, 133, 80, 53, 124);
  expectRGBA(Output, 2, 1, 141, 89, 61, 119);
  expectRGBA(Output, 3, 1, 147, 97, 67, 115);
  expectRGBA(Output, 0, 2, 154, 104, 74, 110);
  expectRGBA(Output, 1, 2, 160, 112, 80, 106);
  expectRGBA(Output, 2, 2, 168, 121, 88, 101);
  expectRGBA(Output, 3, 2, 174, 128, 94, 97);
  expectRGBA(Output, 0, 3, 180, 136, 100, 93);
  expectRGBA(Output, 1, 3, 186, 143, 106, 89);
  expectRGBA(Output, 2, 3, 194, 153, 114, 84);
  expectRGBA(Output, 3, 3, 200, 160, 120, 80);
}

// Mode 1: 2 subsets sharing a single P-bit pair across all 4 endpoints
// (rather than one P-bit per endpoint), exercising the `continue`-based
// bit-skip in the raw-endpoint-extraction loop. No alpha field (defaults
// to opaque). Partition 0.
TEST(BC7DecodeTest, Mode1TwoSubsetSharedPBit) {
  const uint8_t Block[16] = {0x02, 0x0A, 0xFA, 0xB4, 0x94, 0x9C, 0xDD, 0x1E,
                              0x3F, 0x06, 0x13, 0x8D, 0xF5, 0x11, 0x8D, 0xF5};
  uint8_t Output[64];
  decodeBC7Block(Block, Output);
  expectRGBA(Output, 0, 0, 42, 82, 122, 255);
  expectRGBA(Output, 1, 0, 59, 99, 139, 255);
  expectRGBA(Output, 2, 0, 96, 136, 104, 255);
  expectRGBA(Output, 3, 0, 113, 153, 85, 255);
  expectRGBA(Output, 0, 1, 112, 152, 192, 255);
  expectRGBA(Output, 1, 1, 129, 169, 209, 255);
  expectRGBA(Output, 2, 1, 166, 206, 25, 255);
  expectRGBA(Output, 3, 1, 183, 223, 6, 255);
  expectRGBA(Output, 0, 2, 42, 82, 122, 255);
  expectRGBA(Output, 1, 2, 59, 99, 139, 255);
  expectRGBA(Output, 2, 2, 96, 136, 104, 255);
  expectRGBA(Output, 3, 2, 113, 153, 85, 255);
  expectRGBA(Output, 0, 3, 112, 152, 192, 255);
  expectRGBA(Output, 1, 3, 129, 169, 209, 255);
  expectRGBA(Output, 2, 3, 166, 206, 25, 255);
  expectRGBA(Output, 3, 3, 113, 153, 85, 255);
}

// Mode 0: the most complex partition shape (3 subsets), unique P-bits.
// Exercises `kPartitions3`/`kAnchorSecondSubset3`/`kAnchorThirdSubset`.
// Partition 5.
TEST(BC7DecodeTest, Mode0ThreeSubset) {
  const uint8_t Block[16] = {0x2B, 0x94, 0xFA, 0x5C, 0xB6, 0x1C, 0x7B, 0xD8,
                              0x3E, 0x39, 0x23, 0x9A, 0xF5, 0x11, 0x8D, 0xF5};
  uint8_t Output[64];
  decodeBC7Block(Block, Output);
  expectRGBA(Output, 0, 0, 24, 41, 57, 255);
  expectRGBA(Output, 1, 0, 44, 61, 77, 255);
  expectRGBA(Output, 2, 0, 110, 126, 143, 255);
  expectRGBA(Output, 3, 0, 132, 148, 165, 255);
  expectRGBA(Output, 0, 1, 106, 122, 139, 255);
  expectRGBA(Output, 1, 1, 125, 142, 158, 255);
  expectRGBA(Output, 2, 1, 200, 217, 233, 255);
  expectRGBA(Output, 3, 1, 222, 239, 255, 255);
  expectRGBA(Output, 0, 2, 24, 41, 57, 255);
  expectRGBA(Output, 1, 2, 44, 61, 77, 255);
  expectRGBA(Output, 2, 2, 153, 161, 168, 255);
  expectRGBA(Output, 3, 2, 169, 171, 174, 255);
  expectRGBA(Output, 0, 3, 106, 122, 139, 255);
  expectRGBA(Output, 1, 3, 125, 142, 158, 255);
  expectRGBA(Output, 2, 3, 216, 204, 192, 255);
  expectRGBA(Output, 3, 3, 169, 171, 174, 255);
}

// Mode 4: rotation (1, swapping alpha with red in the final output only)
// and index-selection (1, swapping which raw index field drives color
// vs. alpha interpolation), 1 subset.
TEST(BC7DecodeTest, Mode4RotationAndIndexSelection) {
  const uint8_t Block[16] = {0xB0, 0x25, 0x1B, 0x7D, 0x36, 0x25, 0xCB, 0xC9,
                              0xC9, 0xC9, 0x89, 0xC6, 0xFA, 0x88, 0xC6, 0xFA};
  uint8_t Output[64];
  decodeBC7Block(Block, Output);
  expectRGBA(Output, 0, 0, 81, 49, 57, 41);
  expectRGBA(Output, 1, 0, 121, 72, 80, 64);
  expectRGBA(Output, 2, 0, 163, 95, 103, 87);
  expectRGBA(Output, 3, 0, 203, 119, 127, 111);
  expectRGBA(Output, 0, 1, 81, 144, 152, 136);
  expectRGBA(Output, 1, 1, 121, 168, 176, 160);
  expectRGBA(Output, 2, 1, 163, 191, 199, 183);
  expectRGBA(Output, 3, 1, 203, 214, 222, 206);
  expectRGBA(Output, 0, 2, 81, 49, 57, 41);
  expectRGBA(Output, 1, 2, 121, 72, 80, 64);
  expectRGBA(Output, 2, 2, 163, 95, 103, 87);
  expectRGBA(Output, 3, 2, 203, 119, 127, 111);
  expectRGBA(Output, 0, 3, 81, 144, 152, 136);
  expectRGBA(Output, 1, 3, 121, 168, 176, 160);
  expectRGBA(Output, 2, 3, 163, 191, 199, 183);
  expectRGBA(Output, 3, 3, 203, 214, 222, 206);
}

// A fully-zeroed block's first byte has no set bit at all, which is not a
// valid mode encoding. Mirrors CTS's own behavior of decoding such a block
// as fully-transparent black rather than reading out-of-range data.
TEST(BC7DecodeTest, InvalidModeDecodesToTransparentBlack) {
  const uint8_t Block[16] = {};
  uint8_t Output[64];
  decodeBC7Block(Block, Output);
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 0; X != 4; ++X)
      expectRGBA(Output, X, Y, 0, 0, 0, 0);
}

} // namespace
