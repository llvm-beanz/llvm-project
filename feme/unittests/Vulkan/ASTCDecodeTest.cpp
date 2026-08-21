//===- ASTCDecodeTest.cpp - ASTC LDR block decoder tests -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTCDecode.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>

using namespace feme::vulkan;

namespace {

/// Sets bits `[Start, Start + Len)` of \p Block (bit 0 the LSB of byte 0,
/// matching ASTCDecode.cpp's own convention) to \p Value's low \p Len
/// bits, leaving every other bit untouched -- the inverse of
/// `getBits`/`decodeISESequence`'s reading convention, used here only to
/// hand-construct test blocks.
void setBits(uint8_t Block[16], unsigned Start, unsigned Len,
            uint32_t Value) {
  for (unsigned I = 0; I != Len; ++I) {
    unsigned BitIndex = Start + I;
    unsigned Byte = BitIndex / 8, Bit = BitIndex % 8;
    if ((Value >> I) & 1)
      Block[Byte] |= uint8_t(1u << Bit);
    else
      Block[Byte] &= uint8_t(~(1u << Bit));
  }
}

/// A single 4x4-texel ASTC block: one partition, an exact 4x4 weight
/// grid (`B4_A2` block mode, `A = 2`/`B = 0`, weight range 2 -- three
/// quantization levels, {0, 32, 64}), and color endpoint mode 8 (LDR RGB,
/// direct) with its 6 raw values stored at range 255 (a plain 8-bit
/// value each, no trit/quint math needed to hand-encode them). Leaves
/// every weight-grid trit at its default (all-zero block bits decode to
/// weight 0 everywhere, i.e. every texel equals `Lo` exactly) unless the
/// caller overwrites the weight region afterwards.
///
/// See this file's own derivation in agent_thoughts.md for why these
/// particular bit positions/values were chosen (this is the one
/// hand-constructed block mode used throughout this file, reused with
/// different color/weight bits per test).
std::array<uint8_t, 16> makeRGBDirectBlock(uint8_t V0, uint8_t V1, uint8_t V2,
                                           uint8_t V3, uint8_t V4,
                                           uint8_t V5) {
  std::array<uint8_t, 16> Block{};
  // Block mode: Low01 = 1 (bit0), Sub = 0 (B4_A2, bits2-3), A = 2 (bits5-6,
  // weight height = A + 2 = 4), B = 0 (bits7-8, weight width = B + 4 = 4),
  // R low bit = 1 (bit4, combined with Low01<<1 gives weight range 2), H
  // = 0 (bit9), dual-plane = 0 (bit10).
  setBits(Block.data(), 0, 1, 1);
  setBits(Block.data(), 4, 1, 1);
  setBits(Block.data(), 5, 2, 2);
  // Partition count field (bits11-12) = 0 -> 1 partition.
  // Color endpoint mode (bits13-16) = 8 -> LDR RGB, direct.
  setBits(Block.data(), 13, 4, 8);
  // Six raw color values (range 255, 8 bits each) starting at bit 17.
  setBits(Block.data(), 17, 8, V0);
  setBits(Block.data(), 25, 8, V1);
  setBits(Block.data(), 33, 8, V2);
  setBits(Block.data(), 41, 8, V3);
  setBits(Block.data(), 49, 8, V4);
  setBits(Block.data(), 57, 8, V5);
  return Block;
}

/// Sets every one of the 16 weight-grid trits in a `makeRGBDirectBlock`
/// block to \p Trit (0, 1, or 2), by writing the ISE-encoded byte whose
/// `TritEncodings` entry is `{Trit, Trit, Trit, Trit, Trit}` into each of
/// the three full 5-value groups' 8 encoded bits, and the 2-bit prefix
/// that decodes the same trit for the last, 1-value group. Weight data
/// occupies the block's high end, read in reverse bit order (see
/// ASTCDecode.cpp's `getBitsFromEnd` comment) -- bit `i` of the weight
/// stream is bit `127 - i` of the block, so writing weight-stream bit
/// `i` sets block bit `127 - i`.
void setUniformWeights(uint8_t Block[16], unsigned Trit) {
  // TritEncodings[0] == {0,0,0,0,0}, TritEncodings[2] == {2,0,0,0,0}
  // (whose first trit already gives the value the 1-value last group
  // needs), TritEncodings[126] == {2,2,2,2,2} -- the only two full-group
  // encodings this test needs, found by inspecting the table
  // ASTCDecode.cpp embeds (see agent_thoughts.md).
  unsigned FullGroupCode = Trit == 0 ? 0 : 126;
  unsigned LastGroupCode = Trit == 0 ? 0 : 2;
  auto setWeightStreamBits = [&](unsigned StreamStart, unsigned Len,
                                 uint32_t Value) {
    for (unsigned I = 0; I != Len; ++I) {
      unsigned BlockBit = 127 - (StreamStart + I);
      unsigned Byte = BlockBit / 8, Bit = BlockBit % 8;
      if ((Value >> I) & 1)
        Block[Byte] |= uint8_t(1u << Bit);
      else
        Block[Byte] &= uint8_t(~(1u << Bit));
    }
  };
  setWeightStreamBits(0, 8, FullGroupCode);
  setWeightStreamBits(8, 8, FullGroupCode);
  setWeightStreamBits(16, 8, FullGroupCode);
  setWeightStreamBits(24, 2, LastGroupCode);
}

void setBitsAt(uint8_t Block[16], unsigned Start, unsigned Len,
              uint32_t Value) {
  setBits(Block, Start, Len, Value);
}

TEST(ASTCDecodeTest, VoidExtentDecodesToSolidColor) {
  std::array<uint8_t, 16> Block{};
  setBitsAt(Block.data(), 0, 9, 0x1FC); // Void extent signature.
  setBitsAt(Block.data(), 9, 1, 0);     // LDR.
  setBitsAt(Block.data(), 10, 2, 0x3);  // Reserved bits, both 1.
  // Coordinates (bits 12-63) are irrelevant to a solid-fill decode; left
  // zero. Color: R=65535 (-> 255), G=0 (-> 0), B=32767 (-> ~127), A=65535.
  setBitsAt(Block.data(), 64, 16, 65535);
  setBitsAt(Block.data(), 80, 16, 0);
  setBitsAt(Block.data(), 96, 16, 32767);
  setBitsAt(Block.data(), 112, 16, 65535);

  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 255);
    EXPECT_EQ(Out[I * 4 + 1], 0);
    EXPECT_EQ(Out[I * 4 + 2], (32767 * 255) / 65535);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, VoidExtentHDRFallsBackToOpaqueBlack) {
  // Roadmap E20 is LDR-only (see ASTCDecode.h): an HDR void extent block
  // (the profile bit set) decodes to the documented safe fallback rather
  // than being misinterpreted as LDR data.
  std::array<uint8_t, 16> Block{};
  setBitsAt(Block.data(), 0, 9, 0x1FC);
  setBitsAt(Block.data(), 9, 1, 1); // HDR.
  setBitsAt(Block.data(), 10, 2, 0x3);

  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 0);
    EXPECT_EQ(Out[I * 4 + 1], 0);
    EXPECT_EQ(Out[I * 4 + 2], 0);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, ReservedBlockModeFallsBackToOpaqueBlack) {
  // Low01 == 0 and the four-bit family selector's top two bits are also
  // both 0 with the low nibble 0 -- a reserved encoding no encoder
  // produces (specification "Reserved block mode" case), which must
  // still decode to something well-defined rather than read out of
  // bounds or assert.
  std::array<uint8_t, 16> Block{};
  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 0);
    EXPECT_EQ(Out[I * 4 + 1], 0);
    EXPECT_EQ(Out[I * 4 + 2], 0);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, RGBDirectAllZeroWeightsMatchesLowEndpoint) {
  // Every texel's weight decodes to 0 (see `makeRGBDirectBlock`'s
  // comment), so every texel should equal the low endpoint exactly:
  // Lo = (v0, v2, v4, 255) since v0+v2+v4 (30+70+110=210) <
  // v1+v3+v5 (150+190+230=570), so `RGBDirect`'s own no-swap branch
  // applies (specification "Color Endpoint Mode 8").
  auto Block = makeRGBDirectBlock(30, 150, 70, 190, 110, 230);

  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 30);
    EXPECT_EQ(Out[I * 4 + 1], 70);
    EXPECT_EQ(Out[I * 4 + 2], 110);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, RGBDirectAllMaxWeightsMatchesHighEndpoint) {
  // Every texel's weight instead decodes to 64 (full weight): every
  // texel should equal the high endpoint, Hi = (v1, v3, v5, 255).
  auto Block = makeRGBDirectBlock(30, 150, 70, 190, 110, 230);
  setUniformWeights(Block.data(), /*Trit=*/2);

  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 150);
    EXPECT_EQ(Out[I * 4 + 1], 190);
    EXPECT_EQ(Out[I * 4 + 2], 230);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, RGBDirectSwapsWhenSecondEndpointSumIsSmaller) {
  // v0+v2+v4 (200+210+220=630) > v1+v3+v5 (10+20+30=60): the low/high
  // roles swap and both endpoints run through `blue_contract`
  // (specification "Color Endpoint Mode 8", the sum-comparison branch).
  auto Block = makeRGBDirectBlock(200, 10, 210, 20, 220, 30);

  std::array<uint8_t, 4 * 4 * 4> Out{};
  decodeASTCBlock(Block.data(), 4, 4, Out.data());
  // Every texel's weight is 0 (default), so the result is whichever
  // endpoint ends up as "low" post-swap: originally-Hi (10, 20, 30),
  // blue-contracted -- R' = (R + B) / 2, G' = (G + B) / 2, B unchanged.
  int R = (10 + 30) >> 1;
  int G = (20 + 30) >> 1;
  int B = 30;
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], R);
    EXPECT_EQ(Out[I * 4 + 1], G);
    EXPECT_EQ(Out[I * 4 + 2], B);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

TEST(ASTCDecodeTest, LargerFootprintDecodesEveryTexel) {
  // The same block mode/weight-grid bits, decoded against an 8x8
  // footprint instead of 4x4: the weight grid stays 4x4 (the block's own
  // bits do not change), so `infillWeights` upsamples it across 64
  // texels instead of mapping 1:1 -- exercised here only for "does not
  // crash and produces the uniform-weight endpoint everywhere" (every
  // grid weight is still 0), not the interpolated case.
  auto Block = makeRGBDirectBlock(30, 150, 70, 190, 110, 230);

  std::array<uint8_t, 8 * 8 * 4> Out{};
  decodeASTCBlock(Block.data(), 8, 8, Out.data());
  for (unsigned I = 0; I != 64; ++I) {
    EXPECT_EQ(Out[I * 4 + 0], 30);
    EXPECT_EQ(Out[I * 4 + 1], 70);
    EXPECT_EQ(Out[I * 4 + 2], 110);
    EXPECT_EQ(Out[I * 4 + 3], 255);
  }
}

} // namespace
