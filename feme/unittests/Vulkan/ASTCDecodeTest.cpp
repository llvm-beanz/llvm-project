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

//===----------------------------------------------------------------------===//
// Roadmap E21 (`decodeASTCBlockHDR`): void-extent and the six HDR-only
// color endpoint modes (2, 3, 7, 11, 14, 15).
//===----------------------------------------------------------------------===//

/// Same block-mode/weight-grid bits as `makeRGBDirectBlock` (1 partition,
/// an exact 4x4 weight grid, all weights defaulting to 0), but with
/// \p Cem raw color values (\p NumValues of them) at range 255 starting
/// at bit 17 instead of always 6 values at CEM 8 -- reused by every HDR
/// color endpoint mode test below, which each need a different value
/// count.
std::array<uint8_t, 16> makeHDRBlock(unsigned Cem,
                                     const std::vector<uint8_t> &Values) {
  std::array<uint8_t, 16> Block{};
  setBits(Block.data(), 0, 1, 1);
  setBits(Block.data(), 4, 1, 1);
  setBits(Block.data(), 5, 2, 2);
  setBits(Block.data(), 13, 4, Cem);
  unsigned Bit = 17;
  for (uint8_t Val : Values) {
    setBits(Block.data(), Bit, 8, Val);
    Bit += 8;
  }
  return Block;
}

TEST(ASTCDecodeTest, HDRVoidExtentDecodesStoredHalfFloats) {
  // Bit 9 set (HDR dynamic range): the four 16-bit fields are FP16 bit
  // patterns, not UNORM16 values. 0x3C00 is FP16 1.0, 0x0000 is 0.0,
  // 0x4000 is 2.0.
  std::array<uint8_t, 16> Block{};
  setBits(Block.data(), 0, 9, 0x1FC);
  setBits(Block.data(), 9, 1, 1); // HDR.
  setBits(Block.data(), 10, 2, 0x3);
  setBits(Block.data(), 64, 16, 0x3C00);
  setBits(Block.data(), 80, 16, 0x0000);
  setBits(Block.data(), 96, 16, 0x4000);
  setBits(Block.data(), 112, 16, 0x3C00);

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 2.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, LDRVoidExtentThroughHDREntryPointMatchesUnormValues) {
  // Bit 9 clear (LDR dynamic range): the same UNORM16-stored color
  // `decodeASTCBlock`'s own `VoidExtentDecodesToSolidColor` test uses,
  // now decoded through the HDR entry point -- should divide down to a
  // `[0, 1]` float instead of an 8-bit value, not be misread as FP16.
  std::array<uint8_t, 16> Block{};
  setBits(Block.data(), 0, 9, 0x1FC);
  setBits(Block.data(), 9, 1, 0); // LDR.
  setBits(Block.data(), 10, 2, 0x3);
  setBits(Block.data(), 64, 16, 65535);
  setBits(Block.data(), 80, 16, 0);
  setBits(Block.data(), 96, 16, 32767);
  setBits(Block.data(), 112, 16, 65535);

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 32767.0f / 65535.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRLuminanceLargeRangeCEM2InterpolatesBothEndpoints) {
  // CEM 2 ("HDR luminance, large range"): 2 raw values, V1 (200) >= V0
  // (100), so y0 = V0<<4 = 1600, y1 = V1<<4 = 3200 (specification "HDR
  // Endpoint Mode 2"). Alpha is the mode's own constant 0x780 (FP16
  // 1.0). Weight 0 (all-zero grid, see `makeHDRBlock`'s reuse of
  // `makeRGBDirectBlock`'s block mode) selects the low endpoint
  // everywhere.
  auto Block = makeHDRBlock(/*Cem=*/2, {100, 200});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  // y0 = 1600 -> 16-bit 0x6400 -> E=12, M=1024 -> Mt=4*1024-512=3584 ->
  // Cf=(12<<10)+(3584>>3)=0x31C0 -> half 1.4375 * 2^-3 = 0.1796875.
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 0.1796875f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.1796875f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.1796875f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }

  setUniformWeights(Block.data(), /*Trit=*/2); // Full weight -> high endpoint.
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  // y1 = 3200 -> 16-bit 3200<<4=51200=0xC800 -> E=(0xC800&0xF800)>>11=25,
  // M=0 -> Mt=0 -> Cf=25<<10=0x6400 -> half exp=25, mantissa=0 ->
  // 1.0 * 2^(25-15) = 1024.0.
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1024.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 1024.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 1024.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRLuminanceSmallRangeCEM3DecodesBaseValue) {
  // CEM 3 ("HDR luminance, small range"): V0's top bit clear selects the
  // "else" branch (specification "HDR Endpoint Mode 3"). V0 = 0x10 (top
  // bit clear, low 7 bits 0x10), V1 = 0x20 -> y0 = ((0x20&0xF0)<<4) |
  // ((0x10&0x7F)<<1) = (0x20<<4)|(0x10<<1) = 0x200|0x20 = 0x220. d =
  // (0x20&0xF)<<1 = 0. y1 = y0 + 0 = 0x220 (identical to y0, since d=0).
  auto Block = makeHDRBlock(/*Cem=*/3, {0x10, 0x20});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  // y0 = 0x220 = 544 -> 16-bit (544<<4)=8704=0x2200 -> E=(0x2200&0xF800)
  // >>11=4, M=0x2200&0x7FF=0x200=512 -> Mt=4*512-512=1536 ->
  // Cf=(4<<10)+(1536>>3)=4096+192=4288=0x10C0 -> half exp=4,
  // mantissa=0xC0=192 -> (1+192/1024)*2^(4-15)=1.1875*2^-11.
  float Expected = 1.1875f / 2048.0f;
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], Expected);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], Expected);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], Expected);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRRGBBaseScaleCEM7Mode5InterpolatesBothEndpoints) {
  // CEM 7 ("HDR RGB, base + scale"), sub-mode 5 (the simplest: no extra
  // bits relocate into red/green/blue, `majcomp` 0, no swaps). Chosen so
  // that G/B end up 0 for both endpoints and R is easy to hand-verify
  // (specification "HDR Endpoint Mode 7"): V0 = 0xFF (top 2 bits set ->
  // contributes 3 to `modeval`; red_low6 = 0x3F), V1 = 0x80 (top bit set
  // -> contributes 4; green/x0/x1 all 0), V2 = 0x80 (top bit set ->
  // contributes 8, giving modeval = 0xF -> majcomp=0, mode=5; blue/x2/x3
  // all 0), V3 = 0x10 (top 3 bits clear -> x4/x5/x6 = 0; scale_low =
  // 0x10).
  auto Block = makeHDRBlock(/*Cem=*/7, {0xFF, 0x80, 0x80, 0x10});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  // red = 0x3F<<5 = 2016, scale = 0x10<<5 = 512. e0.r = clamp(2016-512) =
  // 1504 -> 16-bit 1504<<4=24064=0x5E00 -> E=11, M=1536 ->
  // Mt=5*1536-2048=5632 -> Cf=(11<<10)+(5632>>3)=0x2EC0 -> half
  // 1.6875*2^-4 = 0.10546875. e0.g = e0.b = clamp(0-512) = 0 -> 0.0.
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 0.10546875f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }

  setUniformWeights(Block.data(), /*Trit=*/2);
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  // e1.r = red = 2016 -> 16-bit 32256=0x7E00 -> E=15, M=1536 ->
  // Mt=5632 -> Cf=(15<<10)+704=0x3EC0 -> half 1.6875*2^0 = 1.6875.
  // e1.g = e1.b = 0.
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.6875f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRRGBDirectCEM11MajorComponentThreeDirectPath) {
  // CEM 11 ("HDR RGB, direct"), the `majcomp == 3` "specify directly"
  // path (both V4 and V5's top bit set -- specification "HDR Endpoint
  // Mode 11"). V0 = 120 -> e0.r = 120<<4 = 1920 = 0x780 (the same value
  // CEM 2/3/7/11's own alpha constant uses, i.e. decodes to 1.0). V2 = 0
  // -> e0.g = 0.0. V4 = 0x80 (top bit set, low 7 bits 0) -> e0.b = 0.0.
  // V1 = 128 -> e1.r = 128<<4 = 2048; widened again to 16 bits (128<<8 =
  // 32768 = 0x8000) decodes to exactly 2.0. V3 = 0, V5 = 0x80 -> e1.g =
  // e1.b = 0.0.
  auto Block = makeHDRBlock(/*Cem=*/11, {120, 128, 0, 0, 0x80, 0x80});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f); // Alpha: mode 11's own 0x780.
  }

  setUniformWeights(Block.data(), /*Trit=*/2);
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 2.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRRGBDirectPlusLDRAlphaCEM14MixesDomains) {
  // CEM 14 ("HDR RGB, direct + LDR alpha"): same RGB values as the CEM
  // 11 test above (so R/G/B repeat that test's already-verified 1.0/2.0
  // and 0.0/0.0 results), plus 2 more raw values (V6, V7) that are LDR
  // (8-bit UNORM) alpha, not HDR (specification "HDR Endpoint Mode 14"
  // -- "alpha values are interpreted... as 8-bit unsigned normalized
  // values"). V6 = 255 -> alpha low endpoint 1.0 exactly (the UNORM8
  // saturating case); V7 = 128 -> alpha high endpoint (128<<8|128) /
  // 65536 = 32896/65536.
  auto Block = makeHDRBlock(/*Cem=*/14, {120, 128, 0, 0, 0x80, 0x80, 255, 128});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }

  setUniformWeights(Block.data(), /*Trit=*/2);
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 2.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 32896.0f / 65536.0f);
  }
}

TEST(ASTCDecodeTest, HDRRGBDirectPlusHDRAlphaCEM15DecodesAlphaMode3) {
  // CEM 15 ("HDR RGB, direct + HDR alpha"), alpha sub-mode 3 ("directly
  // specify alphas" -- specification "HDR Endpoint Mode 15"), selected
  // by setting both V6 and V7's top bit. Same RGB values as the CEM
  // 11/14 tests above. V6 = 0x80|60 -> A (7 low bits) = 60 -> e0.alpha =
  // 60<<5 = 1920 = 0x780, the same constant CEM 2/3/7/11 use for a
  // constant 1.0 alpha. V7 = 0x80|30 -> e1.alpha (12-bit) = 30<<5 = 960
  // -> 16-bit 960<<4=15360=0x3C00 -> E=7, M=1024 -> Mt=4*1024-512=3584
  // -> Cf=(7<<10)+(3584>>3)=7168+448=7616=0x1DC0 -> half
  // 1.4375*2^-8 = 1.4375/256.
  auto Block =
      makeHDRBlock(/*Cem=*/15, {120, 128, 0, 0, 0x80, 0x80, 0xBC, 0x9E});

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }

  setUniformWeights(Block.data(), /*Trit=*/2);
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  float ExpectedAlpha = 1.4375f / 256.0f;
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 2.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], ExpectedAlpha);
  }
}

TEST(ASTCDecodeTest, HDREntryPointDecodesLDRModeThroughSharedPipeline) {
  // A block whose only color endpoint mode is LDR (CEM 8, "LDR RGB,
  // direct" -- the same block `RGBDirectAllZeroWeightsMatchesLowEndpoint`
  // decodes through `decodeASTCBlock`), decoded instead through
  // `decodeASTCBlockHDR`: every channel should divide down to the same
  // `[0, 1]` value `decodeASTCBlock`'s UNORM8 result represents, matching
  // roadmap E21's own "still decodes every LDR mode too" comment
  // (ASTCDecode.h). Per specification "Weight Application", an LDR
  // channel's 8-bit value \p v widens to 16 bits by bit replication
  // (`(v << 8) | v`, not `v * 257.0/255.0`-style scaling) before dividing
  // by 65536, so the expected value below is that exact formula, not a
  // naive `v / 255.0f`.
  auto Block = makeRGBDirectBlock(30, 150, 70, 190, 110, 230);

  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  auto Expand = [](unsigned V) { return ((V << 8) | V) / 65536.0f; };
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], Expand(30));
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], Expand(70));
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], Expand(110));
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

TEST(ASTCDecodeTest, HDRReservedBlockModeDecodesToErrorColor) {
  // The same reserved encoding `ReservedBlockModeFallsBackToOpaqueBlack`
  // uses, decoded through the HDR entry point: the specification's HDR
  // error result (opaque fully-saturated magenta), not LDR's opaque
  // black, applies here (specification "LDR and HDR Modes").
  std::array<uint8_t, 16> Block{};
  std::array<float, 4 * 4 * 4> Out{};
  decodeASTCBlockHDR(Block.data(), 4, 4, Out.data());
  for (unsigned I = 0; I != 16; ++I) {
    EXPECT_FLOAT_EQ(Out[I * 4 + 0], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 1], 0.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 2], 1.0f);
    EXPECT_FLOAT_EQ(Out[I * 4 + 3], 1.0f);
  }
}

} // namespace
