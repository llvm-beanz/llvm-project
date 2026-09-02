//===- BCDecodeTest.cpp - BC1-5 (S3TC/RGTC) block decoder tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BCDecode.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>

using namespace feme::vulkan;

namespace {

/// Writes a little-endian 16-bit value at \p Bytes.
void putU16LE(uint8_t *Bytes, uint16_t V) {
  Bytes[0] = static_cast<uint8_t>(V & 0xFF);
  Bytes[1] = static_cast<uint8_t>((V >> 8) & 0xFF);
}

/// Writes a little-endian 32-bit value at \p Bytes.
void putU32LE(uint8_t *Bytes, uint32_t V) {
  for (unsigned I = 0; I != 4; ++I)
    Bytes[I] = static_cast<uint8_t>((V >> (8 * I)) & 0xFF);
}

/// Packs 16 2-bit codes (`Codes[i]` for texel `i`, in this file's own
/// row-major `4 * y + x` order) into the 4-byte BC1 index field.
uint32_t packIndices2Bit(const unsigned Codes[16]) {
  uint32_t Bits = 0;
  for (unsigned I = 0; I != 16; ++I)
    Bits |= (Codes[I] & 0x3) << (2 * I);
  return Bits;
}

/// Packs 16 3-bit codes into the 48-bit BC3-alpha/BC4/BC5 index field,
/// returned as a 6-byte little-endian sequence written to \p Bytes.
void packIndices3Bit(const unsigned Codes[16], uint8_t Bytes[6]) {
  uint64_t Bits = 0;
  for (unsigned I = 0; I != 16; ++I)
    Bits |= static_cast<uint64_t>(Codes[I] & 0x7) << (3 * I);
  for (unsigned I = 0; I != 6; ++I)
    Bytes[I] = static_cast<uint8_t>((Bits >> (8 * I)) & 0xFF);
}

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

TEST(BCDecodeTest, BC1FourColorMode) {
  // color0 = 0xFFFF (white, RGB565 max), color1 = 0x0000 (black).
  // color1 (0) > color0 (0xFFFF) is false, so FourColorMode = true.
  // Texel 0 -> code 0 (white), 1 -> code 1 (black),
  // 2 -> code 2 ((2*white+black)/3), 3 -> code 3 ((white+2*black)/3).
  uint8_t Block[8] = {};
  putU16LE(Block, 0xFFFF);
  putU16LE(Block + 2, 0x0000);
  unsigned Codes[16] = {0, 1, 2, 3};
  putU32LE(Block + 4, packIndices2Bit(Codes));
  uint8_t Output[64];
  decodeBC1Block(Block, /*HasAlpha=*/false, Output);
  expectRGBA(Output, 0, 0, 255, 255, 255, 255);
  expectRGBA(Output, 1, 0, 0, 0, 0, 255);
  expectRGBA(Output, 2, 0, 170, 170, 170, 255);
  expectRGBA(Output, 3, 0, 85, 85, 85, 255);
}

TEST(BCDecodeTest, BC1ThreeColorModeWithAlpha) {
  // color0 = 0x0000 (black), color1 = 0xFFFF (white).
  // color1 (0xFFFF) > color0 (0) is true, so FourColorMode = false.
  // Texel 0 -> code 0 (black), 1 -> code 1 (white),
  // 2 -> code 2 ((black+white)/2), 3 -> code 3, transparent (HasAlpha).
  uint8_t Block[8] = {};
  putU16LE(Block, 0x0000);
  putU16LE(Block + 2, 0xFFFF);
  unsigned Codes[16] = {0, 1, 2, 3};
  putU32LE(Block + 4, packIndices2Bit(Codes));
  uint8_t Output[64];
  decodeBC1Block(Block, /*HasAlpha=*/true, Output);
  expectRGBA(Output, 0, 0, 0, 0, 0, 255);
  expectRGBA(Output, 1, 0, 255, 255, 255, 255);
  expectRGBA(Output, 2, 0, 127, 127, 127, 255);
  expectRGBA(Output, 3, 0, 0, 0, 0, 0);
}

TEST(BCDecodeTest, BC1ThreeColorModeNoAlphaIsOpaqueBlack) {
  // Same block as above but the RGB-only (no-alpha) variant: code 3
  // decodes to opaque black rather than transparent.
  uint8_t Block[8] = {};
  putU16LE(Block, 0x0000);
  putU16LE(Block + 2, 0xFFFF);
  unsigned Codes[16] = {3};
  putU32LE(Block + 4, packIndices2Bit(Codes));
  uint8_t Output[64];
  decodeBC1Block(Block, /*HasAlpha=*/false, Output);
  expectRGBA(Output, 0, 0, 0, 0, 0, 255);
}

TEST(BCDecodeTest, BC2ExplicitAlphaAndForcedFourColorColor) {
  // Color block: color0 = 0x0000, color1 = 0xFFFF -- would be 3-color
  // mode in a standalone BC1 block, but BC2 always forces 4-color mode
  // for its own embedded color block, so code 3 decodes to an
  // interpolated color, not black/transparent. Alpha block: explicit
  // 4-bit alpha per texel, texel 0 = 0xF (extends to 255), texel 3 =
  // 0x0 (extends to 0).
  uint8_t Block[16] = {};
  // Alpha nibbles: texel0=0xF, texel1=0x0, texel2=0x8, texel3=0x0.
  Block[0] = 0x0F; // texel0 low nibble=0xF, texel1 high nibble=0x0.
  Block[1] = 0x08; // texel2 low nibble=0x8, texel3 high nibble=0x0.
  putU16LE(Block + 8, 0x0000);
  putU16LE(Block + 10, 0xFFFF);
  unsigned Codes[16] = {0, 1, 2, 3};
  putU32LE(Block + 12, packIndices2Bit(Codes));
  uint8_t Output[64];
  decodeBC2Block(Block, Output);
  // extend4to8(0xF) = 255, extend4to8(0x8) = 136, extend4to8(0x0) = 0.
  expectRGBA(Output, 0, 0, 0, 0, 0, 255);
  expectRGBA(Output, 1, 0, 255, 255, 255, 0);
  expectRGBA(Output, 2, 0, 85, 85, 85, 136);
  // Forced four-color mode: code 3 -> (2*color1 + color0)/3 = 170, not
  // black -- the whole point of this test.
  expectRGBA(Output, 3, 0, 170, 170, 170, 0);
}

TEST(BCDecodeTest, BC3InterpolatedAlphaAndForcedFourColorColor) {
  // alpha0 = 200, alpha1 = 50 (alpha0 > alpha1 -> 7-step ramp):
  // table = {200, 50, 178, 157, 135, 114, 92, 71} (verified against a
  // direct Python re-implementation of the same truncating-division
  // formula while writing this test).
  uint8_t Block[16] = {};
  Block[0] = 200;
  Block[1] = 50;
  unsigned AlphaCodes[16] = {0, 1, 2, 3, 4, 5, 6, 7};
  packIndices3Bit(AlphaCodes, Block + 2);
  putU16LE(Block + 8, 0x0000);
  putU16LE(Block + 10, 0xFFFF);
  unsigned ColorCodes[16] = {0};
  putU32LE(Block + 12, packIndices2Bit(ColorCodes));
  uint8_t Output[64];
  decodeBC3Block(Block, Output);
  uint8_t Expected[8] = {200, 50, 178, 157, 135, 114, 92, 71};
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_EQ(texelAt(Output, I, 0).A, Expected[I]) << "alpha texel " << I;
}

TEST(BCDecodeTest, BC4Unsigned) {
  // endpoint0 = 200, endpoint1 = 50 (matches the BC3-alpha table above,
  // since BC4 unsigned uses the identical formula on a single channel).
  uint8_t Block[8] = {};
  Block[0] = 200;
  Block[1] = 50;
  unsigned Codes[16] = {0, 1, 2, 3, 4, 5, 6, 7};
  packIndices3Bit(Codes, Block + 2);
  uint8_t Output[16];
  decodeBC4Block(Block, /*Signed=*/false, Output);
  uint8_t Expected[8] = {200, 50, 178, 157, 135, 114, 92, 71};
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_EQ(Output[I], Expected[I]) << "texel " << I;
}

TEST(BCDecodeTest, BC4SignedSevenStep) {
  // endpoint0 = 100, endpoint1 = -100 (100 > -100 -> 7-step ramp):
  // table = {100, -100, 71, 42, 14, -14, -42, -71} (C++ truncating
  // division towards zero, not floor division -- verified by a direct
  // re-implementation of the same truncating formula while writing
  // this test).
  uint8_t Block[8] = {};
  Block[0] = static_cast<uint8_t>(static_cast<int8_t>(100));
  Block[1] = static_cast<uint8_t>(static_cast<int8_t>(-100));
  unsigned Codes[16] = {0, 1, 2, 3, 4, 5, 6, 7};
  packIndices3Bit(Codes, Block + 2);
  uint8_t Output[16];
  decodeBC4Block(Block, /*Signed=*/true, Output);
  int8_t Expected[8] = {100, -100, 71, 42, 14, -14, -42, -71};
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_EQ(static_cast<int8_t>(Output[I]), Expected[I]) << "texel " << I;
}

TEST(BCDecodeTest, BC4SignedMinMaxMode) {
  // endpoint0 = -100, endpoint1 = 100 (-100 > 100 is false -> min/max
  // mode): table = {-100, 100, -60, -20, 20, 60, -127, 127}.
  uint8_t Block[8] = {};
  Block[0] = static_cast<uint8_t>(static_cast<int8_t>(-100));
  Block[1] = static_cast<uint8_t>(static_cast<int8_t>(100));
  unsigned Codes[16] = {0, 1, 2, 3, 4, 5, 6, 7};
  packIndices3Bit(Codes, Block + 2);
  uint8_t Output[16];
  decodeBC4Block(Block, /*Signed=*/true, Output);
  int8_t Expected[8] = {-100, 100, -60, -20, 20, 60, -127, 127};
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_EQ(static_cast<int8_t>(Output[I]), Expected[I]) << "texel " << I;
}

TEST(BCDecodeTest, BC4SignedNegative128ClampsToNegative127) {
  // endpoint0 byte 0x80 (-128 as two's complement) must decode
  // identically to -127 (the specification's own explicit rule);
  // endpoint1 = 50 (-127 > 50 is false -> min/max mode):
  // table = {-127, 50, -91, -56, -20, 14, -127, 127} (C++ truncating
  // division towards zero).
  uint8_t Block[8] = {};
  Block[0] = 0x80;
  Block[1] = static_cast<uint8_t>(static_cast<int8_t>(50));
  unsigned Codes[16] = {0, 1, 2, 3, 4, 5, 6, 7};
  packIndices3Bit(Codes, Block + 2);
  uint8_t Output[16];
  decodeBC4Block(Block, /*Signed=*/true, Output);
  int8_t Expected[8] = {-127, 50, -91, -56, -20, 14, -127, 127};
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_EQ(static_cast<int8_t>(Output[I]), Expected[I]) << "texel " << I;
}

TEST(BCDecodeTest, BC5UnsignedDualChannel) {
  // Red sub-block: endpoint0=200, endpoint1=50 (7-step). Green
  // sub-block: endpoint0=50, endpoint1=200 (5-step + min/max).
  uint8_t Block[16] = {};
  Block[0] = 200;
  Block[1] = 50;
  unsigned RedCodes[16] = {2, 6};
  packIndices3Bit(RedCodes, Block + 2);
  Block[8] = 50;
  Block[9] = 200;
  unsigned GreenCodes[16] = {6, 7};
  packIndices3Bit(GreenCodes, Block + 10);
  uint8_t Output[32];
  decodeBC5Block(Block, /*Signed=*/false, Output);
  // Red table {200,50,178,157,135,114,92,71}: code2->178, code6->92.
  // Green table (50<=200, min/max mode) {50,200,80,110,140,170,0,255}:
  // code6->0, code7->255.
  EXPECT_EQ(Output[0], 178); // texel0 R
  EXPECT_EQ(Output[1], 0);   // texel0 G
  EXPECT_EQ(Output[2], 92);  // texel1 R
  EXPECT_EQ(Output[3], 255); // texel1 G
}

} // namespace
