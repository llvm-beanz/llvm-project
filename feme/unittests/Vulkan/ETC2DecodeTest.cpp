//===- ETC2DecodeTest.cpp - ETC2/EAC block decoder tests ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ETC2Decode.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace feme::vulkan;

namespace {

/// Sets bits `[Start, Start + Len)` of \p Block, per this whole file's
/// (and `ETC2Decode.cpp`'s own) big-endian "int64bit" bit numbering --
/// bit 0 is the LSB of byte 7 (the *last* memory byte), bit 63 the MSB
/// of byte 0 (the *first*) -- to \p Value's low \p Len bits (bit 0 of
/// \p Value becomes bit `Start` of \p Block), leaving every other bit
/// untouched. The inverse of `getBits`'s reading convention, used here
/// only to hand-construct test blocks.
void setBits(uint8_t Block[8], unsigned Start, unsigned Len, uint32_t Value) {
  for (unsigned I = 0; I != Len; ++I) {
    unsigned BitIndex = Start + I;
    unsigned Byte = 7 - BitIndex / 8, Bit = BitIndex % 8;
    if ((Value >> I) & 1)
      Block[Byte] |= uint8_t(1u << Bit);
    else
      Block[Byte] &= uint8_t(~(1u << Bit));
  }
}

/// Sets a value split across multiple non-adjacent bitfields, \p Chunks
/// listed least-significant chunk first -- e.g. planar mode's own
/// Origin-B field, `(bit48 << 5) | (bits[44:43] << 3) | bits[41:39]`, is
/// `{{39, 3}, {43, 2}, {48, 1}}`. Mirrors `ETC2Decode.cpp`'s own
/// composite-field reads in reverse.
struct Chunk {
  unsigned Start, Len;
};
void setCompositeField(uint8_t Block[8], std::vector<Chunk> Chunks,
                       uint32_t Value) {
  for (const Chunk &C : Chunks) {
    setBits(Block, C.Start, C.Len, Value & ((1u << C.Len) - 1));
    Value >>= C.Len;
  }
}

/// Pixel (X, Y)'s own raw 2-bit index (individual/differential mode's
/// own `[2,3,1,0]`-remapped convention; T/H mode instead use this raw
/// value directly as a paint-color selector, per `ETC2Decode.cpp`'s own
/// `colorFor`), split across the block's own "more significant"/"less
/// significant" 16-bit halves.
void setRawPixelIndex(uint8_t Block[8], unsigned X, unsigned Y,
                       unsigned Raw) {
  unsigned Pix = X * 4 + Y;
  setBits(Block, 16 + Pix, 1, (Raw >> 1) & 1);
  setBits(Block, Pix, 1, Raw & 1);
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

TEST(ETC2DecodeTest, IndividualMode) {
  // Individual mode (diff = 0), flip = 0 (left/right 2x4 split). Two
  // distinct flat 4-bit-per-channel base colors, table index 0 for both
  // subblocks, every pixel left at its default raw index 0 (remaps to
  // modifier-table row 2, "+a" = +2 for table 0).
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 0);
  setBits(Block, 32, 1, 0);
  setBits(Block, 60, 4, 10);
  setBits(Block, 52, 4, 5);
  setBits(Block, 44, 4, 2);
  setBits(Block, 56, 4, 1);
  setBits(Block, 48, 4, 12);
  setBits(Block, 40, 4, 8);
  setBits(Block, 37, 3, 0);
  setBits(Block, 34, 3, 0);
  uint8_t Output[64];
  decodeETC2Block(Block, Output);
  // extend4to8(10)=170, extend4to8(5)=85, extend4to8(2)=34, each +2.
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 0; X != 2; ++X)
      expectRGBA(Output, X, Y, 172, 87, 36, 255);
  // extend4to8(1)=17, extend4to8(12)=204, extend4to8(8)=136, each +2.
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 2; X != 4; ++X)
      expectRGBA(Output, X, Y, 19, 206, 138, 255);
}

TEST(ETC2DecodeTest, DifferentialMode) {
  // Differential mode (diff = 1), flip = 0. Base (10, 5, 2), deltas
  // (+1, +1, +1) -> base2 (11, 6, 3). Every pixel's raw index set to 1
  // (remaps to modifier-table row 3, "+b" = +8 for table 0).
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 1);
  setBits(Block, 32, 1, 0);
  setBits(Block, 59, 5, 10);
  setBits(Block, 51, 5, 5);
  setBits(Block, 43, 5, 2);
  setBits(Block, 56, 3, 1);
  setBits(Block, 48, 3, 1);
  setBits(Block, 40, 3, 1);
  setBits(Block, 37, 3, 0);
  setBits(Block, 34, 3, 0);
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 0; X != 4; ++X)
      setRawPixelIndex(Block, X, Y, 1);
  uint8_t Output[64];
  decodeETC2Block(Block, Output);
  // extend5to8(10)=82, extend5to8(5)=41, extend5to8(2)=16, each +8.
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 0; X != 2; ++X)
      expectRGBA(Output, X, Y, 90, 49, 24, 255);
  // extend5to8(11)=90, extend5to8(6)=49, extend5to8(3)=24, each +8.
  for (unsigned Y = 0; Y != 4; ++Y)
    for (unsigned X = 2; X != 4; ++X)
      expectRGBA(Output, X, Y, 98, 57, 32, 255);
}

TEST(ETC2DecodeTest, TMode) {
  // The specification's own worked T-mode example: base colors
  // (13, 1, 8) / (4, 12, 13) (4-bit-per-channel), distance-table index 5
  // (distance 32), giving paint colors (221,17,136) / (100,236,253) /
  // (68,204,221) / (36,172,189) -- verified directly against the
  // specification's own text while designing this decoder.
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 1); // Op/D bit: must be 1 to leave the "always
                            // individual if unset" shortcut and reach
                            // the shared overflow-based mode test.
  // Force R + Rd to overflow (picks T mode) while T's own R1 field
  // (bits 59-60, 56-57) still reads out to 13 (0b1101).
  setBits(Block, 59, 1, 1);
  setBits(Block, 60, 1, 1);
  setBits(Block, 56, 1, 1);
  setBits(Block, 57, 1, 0);
  setBits(Block, 61, 1, 1);
  setBits(Block, 62, 1, 1);
  setBits(Block, 63, 1, 1); // R (bits59-63) = 31
  setBits(Block, 58, 1, 0); // Rd (bits56-58) = 1 -> R+Rd = 32, overflows.
  setBits(Block, 52, 4, 1);  // G1 = 1
  setBits(Block, 48, 4, 8);  // B1 = 8
  setBits(Block, 44, 4, 4);  // R2 = 4
  setBits(Block, 40, 4, 12); // G2 = 12
  setBits(Block, 36, 4, 13); // B2 = 13
  setBits(Block, 34, 2, 2);  // da = 2
  setBits(Block, 32, 1, 1);  // db = 1 -> distance index 5 -> distance 32
  setRawPixelIndex(Block, 0, 0, 0);
  setRawPixelIndex(Block, 1, 0, 1);
  setRawPixelIndex(Block, 2, 0, 2);
  setRawPixelIndex(Block, 3, 0, 3);
  uint8_t Output[64];
  decodeETC2Block(Block, Output);
  expectRGBA(Output, 0, 0, 221, 17, 136, 255);
  expectRGBA(Output, 1, 0, 100, 236, 253, 255);
  expectRGBA(Output, 2, 0, 68, 204, 221, 255);
  expectRGBA(Output, 3, 0, 36, 172, 189, 255);
}

TEST(ETC2DecodeTest, HMode) {
  // Base colors (10, 5, 6) / (2, 8, 12) (4-bit-per-channel, so R+Rd is
  // kept in-range while G+Gd is forced to overflow, selecting H mode),
  // da=1, db=0; the distance index's own least-significant bit is
  // computed (base0 > base1 numerically, so cmp=1), giving distance
  // index 5 (distance 32).
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 1);
  setBits(Block, 63, 1, 0); // R (bits59-63) = R1 exactly (no overflow).
  setBits(Block, 59, 4, 10); // H's own R1 field.
  setBits(Block, 58, 1, 0);
  setBits(Block, 57, 1, 1);
  setBits(Block, 56, 1, 0); // Rd (bits56-58) = 2 -> R+Rd = 12, in range.
  setBits(Block, 52, 1, 1); // G1 LSB.
  setBits(Block, 51, 1, 0); // G (bit51, also B1's MSB) = 0.
  setBits(Block, 53, 1, 1);
  setBits(Block, 54, 1, 1);
  setBits(Block, 55, 1, 1); // G (bits51-55) = 30.
  setBits(Block, 47, 1, 0);
  setBits(Block, 48, 1, 1);
  setBits(Block, 49, 1, 1);
  setBits(Block, 50, 1, 0); // Gd (bits48-50) = 3 -> G+Gd = 33, overflows.
  setBits(Block, 43, 4, 2);  // R2
  setBits(Block, 39, 4, 8);  // G2
  setBits(Block, 35, 4, 12); // B2
  setBits(Block, 34, 1, 1);  // da
  setBits(Block, 32, 1, 0);  // db
  setRawPixelIndex(Block, 0, 0, 0);
  setRawPixelIndex(Block, 1, 0, 1);
  setRawPixelIndex(Block, 2, 0, 2);
  setRawPixelIndex(Block, 3, 0, 3);
  uint8_t Output[64];
  decodeETC2Block(Block, Output);
  // Base0 = (extend4to8(10), extend4to8(5), extend4to8(6)) = (170,85,102).
  // Base1 = (extend4to8(2), extend4to8(8), extend4to8(12)) = (34,136,204).
  // Base0 > Base1 numerically -> cmp=1 -> index (1<<2)|(0<<1)|1 = 5 -> d=32.
  expectRGBA(Output, 0, 0, 202, 117, 134, 255); // Base0 + 32
  expectRGBA(Output, 1, 0, 138, 53, 70, 255);   // Base0 - 32
  expectRGBA(Output, 2, 0, 66, 168, 236, 255);  // Base1 + 32
  expectRGBA(Output, 3, 0, 2, 104, 172, 255);   // Base1 - 32
}

TEST(ETC2DecodeTest, PlanarMode) {
  // Origin (16, 32, 30), Horizontal (32, 64, 32), Vertical (8, 16, 8)
  // (6:7:6-bit RGB each). The G and B channels of Origin, and the R
  // channel of Horizontal, are the composite (bit-scattered) fields;
  // every other channel is a single contiguous field. Origin B is
  // chosen as 30 (rather than a rounder number) specifically because
  // its own bits happen to double as most of the shared mode-selection
  // "B"/"Bd" overflow-test field (bits 43-47/40-42) -- forcing B + Bd to
  // overflow (selecting planar mode) needs values consistent with
  // whatever Origin B is actually encoded as, not an independent choice.
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 1); // Bypass the "diff bit unset -> individual
                            // mode" shortcut so the shared overflow test
                            // below actually runs.
  setBits(Block, 57, 6, 16); // Origin R (single field).
  setCompositeField(Block, {{49, 6}, {56, 1}}, 32); // Origin G.
  setCompositeField(Block, {{39, 3}, {43, 2}, {48, 1}}, 30); // Origin B.
  setCompositeField(Block, {{32, 1}, {34, 5}}, 32); // Horizontal R.
  setBits(Block, 25, 7, 64); // Horizontal G.
  setBits(Block, 19, 6, 32); // Horizontal B.
  setBits(Block, 13, 6, 8);  // Vertical R.
  setBits(Block, 6, 7, 16);  // Vertical G.
  setBits(Block, 0, 6, 8);   // Vertical B.
  // Origin B = 30 already set bits 39-41 = 0b110 and bits 43-44 = 0b11
  // above (its own chunk1/chunk2), which are also B's own low bits (43,
  // 44) and Bd's own low 2 bits (40, 41) in the shared overflow-test
  // field; setting B's remaining (free) bits 45-47 to 1 makes B = 31,
  // and Bd's remaining (free) bit 42 is left 0, making Bd = +3 -- so
  // B + Bd = 34, overflowing [0, 31] and selecting planar mode.
  setBits(Block, 45, 1, 1);
  setBits(Block, 46, 1, 1);
  setBits(Block, 47, 1, 1);
  uint8_t Output[64];
  decodeETC2Block(Block, Output);
  // Origin = (extend6to8(16), extend7to8(32), extend6to8(30)) = (65,64,121).
  // Horizontal = (extend6to8(32), extend7to8(64), extend6to8(32)) = (130,129,130).
  // Vertical = (extend6to8(8), extend7to8(16), extend6to8(8)) = (32,32,32).
  // Texel (0,0) always equals Origin exactly: (4*O+2)>>2 == O.
  expectRGBA(Output, 0, 0, 65, 64, 121, 255);
  expectRGBA(Output, 3, 0, 114, 113, 128, 255);
  expectRGBA(Output, 0, 3, 40, 40, 54, 255);
  expectRGBA(Output, 3, 3, 89, 89, 61, 255);
}

TEST(ETC2DecodeTest, PunchthroughAlphaOpaqueBit) {
  // A differential-mode block (no overflow -> differential, the only
  // mode this test exercises) with the opaque bit unset: the alternate
  // (zeroed-small-entry) modifier table applies, and any pixel whose raw
  // index is exactly 2 becomes fully transparent regardless of what its
  // color would otherwise have been.
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 0); // Opaque = 0.
  setBits(Block, 32, 1, 0); // flip = 0.
  setBits(Block, 59, 5, 10);
  setBits(Block, 51, 5, 5);
  setBits(Block, 43, 5, 2);
  setBits(Block, 56, 3, 1);
  setBits(Block, 48, 3, 1);
  setBits(Block, 40, 3, 1);
  setBits(Block, 37, 3, 0);
  setBits(Block, 34, 3, 0);
  setRawPixelIndex(Block, 0, 0, 1); // subblock 0, row3 (kept) -> +8.
  setRawPixelIndex(Block, 1, 0, 0); // subblock 0, row2 (zeroed) -> +0.
  setRawPixelIndex(Block, 2, 0, 2); // subblock 1, forced transparent.
  setRawPixelIndex(Block, 3, 0, 3); // subblock 1, row0 (kept) -> -8.
  uint8_t Output[64];
  decodeETC2PunchthroughAlphaBlock(Block, Output);
  expectRGBA(Output, 0, 0, 90, 49, 24, 255); // (82,41,16) + 8
  expectRGBA(Output, 1, 0, 82, 41, 16, 255); // (82,41,16) + 0
  expectRGBA(Output, 2, 0, 0, 0, 0, 0);      // transparent
  expectRGBA(Output, 3, 0, 82, 41, 16, 255); // (90,49,24) - 8
}

TEST(ETC2DecodeTest, PunchthroughAlphaOpaqueBitSet) {
  // Same block as above but with the opaque bit set: the normal
  // (unzeroed) modifier table applies and no pixel is transparent, even
  // one whose raw index is 2.
  uint8_t Block[8] = {};
  setBits(Block, 33, 1, 1); // Opaque = 1.
  setBits(Block, 32, 1, 0);
  setBits(Block, 59, 5, 10);
  setBits(Block, 51, 5, 5);
  setBits(Block, 43, 5, 2);
  setBits(Block, 56, 3, 1);
  setBits(Block, 48, 3, 1);
  setBits(Block, 40, 3, 1);
  setBits(Block, 37, 3, 0);
  setBits(Block, 34, 3, 0);
  setRawPixelIndex(Block, 2, 0, 2); // subblock 1, row1 (NOT zeroed here).
  uint8_t Output[64];
  decodeETC2PunchthroughAlphaBlock(Block, Output);
  // ModifierTable[0][1] = -2 -> base2 (90,49,24) - 2.
  expectRGBA(Output, 2, 0, 88, 47, 22, 255);
}

TEST(ETC2DecodeTest, EACUnsigned) {
  uint8_t Block[8] = {};
  setBits(Block, 56, 8, 100); // base
  setBits(Block, 52, 4, 3);   // multiplier
  setBits(Block, 48, 4, 0);   // table index 0
  // Pixel (0,0) index code = 4 -> EACModifierTable[0][4] = 2.
  setBits(Block, 45, 3, 4);
  uint16_t Output[16];
  decodeEACBlock(Block, /*Signed=*/false, Output);
  // value11 = clamp(100*8 + 4 + 2*3*8, 0, 2047) = 852.
  // result16 = (852 << 5) + (852 >> 6) = 27264 + 13 = 27277.
  EXPECT_EQ(Output[0], 27277);
}

TEST(ETC2DecodeTest, EACUnsignedMultiplierZero) {
  uint8_t Block[8] = {};
  setBits(Block, 56, 8, 50); // base
  setBits(Block, 52, 4, 0);  // multiplier = 0 -> unscaled modifier.
  setBits(Block, 48, 4, 2);  // table index 2
  // Pixel (0,0) index code = 0 -> EACModifierTable[2][0] = -2.
  setBits(Block, 45, 3, 0);
  uint16_t Output[16];
  decodeEACBlock(Block, /*Signed=*/false, Output);
  // value11 = clamp(50*8 + 4 + (-2), 0, 2047) = 402.
  // result16 = (402 << 5) + (402 >> 6) = 12864 + 6 = 12870.
  EXPECT_EQ(Output[0], 12870);
}

TEST(ETC2DecodeTest, EACSignedPositive) {
  uint8_t Block[8] = {};
  setBits(Block, 56, 8, 20); // base = +20
  setBits(Block, 52, 4, 2);  // multiplier
  setBits(Block, 48, 4, 1);  // table index 1
  // Pixel (0,0) index code = 4 -> EACModifierTable[1][4] = 2.
  setBits(Block, 45, 3, 4);
  uint16_t Output[16];
  decodeEACBlock(Block, /*Signed=*/true, Output);
  // value11 = clamp(20*8 + 2*2*8, -1023, 1023) = 192.
  // magExt = (192 << 5) + (192 >> 5) = 6144 + 6 = 6150.
  EXPECT_EQ(static_cast<int16_t>(Output[0]), 6150);
}

TEST(ETC2DecodeTest, EACSignedNegative) {
  uint8_t Block[8] = {};
  setBits(Block, 56, 8, 236); // base = -20 (two's complement 8-bit).
  setBits(Block, 52, 4, 1);   // multiplier
  setBits(Block, 48, 4, 0);   // table index 0
  // Pixel (0,0) index code = 0 -> EACModifierTable[0][0] = -3.
  setBits(Block, 45, 3, 0);
  uint16_t Output[16];
  decodeEACBlock(Block, /*Signed=*/true, Output);
  // value11 = clamp(-20*8 + (-3*1*8), -1023, 1023) = -184.
  // magExt = (184 << 5) + (184 >> 5) = 5888 + 5 = 5893.
  EXPECT_EQ(static_cast<int16_t>(Output[0]), -5893);
}

TEST(ETC2DecodeTest, EACSignedBaseNegative128ClampsToNegative127) {
  // Base codeword -128 (0x80) is not a valid encoder output (the
  // specification restricts it to [-127, 127]), but a decoder must
  // still treat it as -127 rather than misbehave.
  uint8_t Block[8] = {};
  setBits(Block, 56, 8, 128); // base = -128 (two's complement), clamps to -127
  setBits(Block, 52, 4, 0);   // multiplier = 0
  setBits(Block, 48, 4, 0);   // table index 0
  // Pixel (0,0) index code = 0 -> EACModifierTable[0][0] = -3.
  setBits(Block, 45, 3, 0);
  uint16_t Output[16];
  decodeEACBlock(Block, /*Signed=*/true, Output);
  // value11 = clamp(-127*8 + (-3), -1023, 1023) = -1019.
  // magExt = (1019 << 5) + (1019 >> 5) = 32608 + 31 = 32639.
  EXPECT_EQ(static_cast<int16_t>(Output[0]), -32639);
}

} // namespace
