//===- BCDecode.cpp - BC1-5 (S3TC/RGTC) block decoder --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BCDecode.h"

#include <cstdint>

using namespace feme::vulkan;

namespace {

/// Loads \p Count (<= 8) bytes starting at \p Bytes as a little-endian
/// unsigned integer -- every BC1-5 multi-byte field (`color0`/`color1`,
/// the alpha/color index bitfields) is specified byte-by-byte with the
/// first (lowest-address) byte contributing the *least* significant
/// bits, unlike `ETC2Decode.cpp`'s own big-endian "int64bit" block
/// convention.
uint64_t loadLE(const uint8_t *Bytes, unsigned Count) {
  uint64_t V = 0;
  for (unsigned I = Count; I != 0; --I)
    V = (V << 8) | Bytes[I - 1];
  return V;
}

/// Bits `[Start, Start + Len)` of \p V, as an unsigned integer with bit 0
/// of the result equal to bit `Start` of \p V.
uint32_t getBits(uint64_t V, unsigned Start, unsigned Len) {
  if (Len == 0)
    return 0;
  return static_cast<uint32_t>((V >> Start) & ((uint64_t(1) << Len) - 1));
}

uint8_t extend5to8(uint32_t V) {
  return static_cast<uint8_t>((V << 3) | (V >> 2));
}
uint8_t extend6to8(uint32_t V) {
  return static_cast<uint8_t>((V << 2) | (V >> 4));
}

/// A texel's own linear index within a 4x4 block, per BC1-5's own
/// row-major convention (`code(x, y)`/`alpha(x, y)`'s own
/// `4 * y + x` addressing throughout the specification) -- the opposite
/// convention from `ETC2Decode.cpp`'s own column-major `x * 4 + y`
/// pixel-letter numbering; deliberately named differently
/// (`linearIndex`, not `rawPixelIndex`) to make that difference visible
/// at every call site rather than risk silently reusing the wrong
/// convention.
unsigned linearIndex(unsigned X, unsigned Y) { return 4 * Y + X; }

struct RGB {
  uint8_t R, G, B;
};

/// Unpacks a 16-bit RGB565 color word (`R` in the high 5 bits, `G` in
/// the middle 6, `B` in the low 5) to an 8-bit-per-channel `RGB`.
RGB unpack565(uint32_t Color) {
  return {extend5to8(getBits(Color, 11, 5)), extend6to8(getBits(Color, 5, 6)),
          extend5to8(getBits(Color, 0, 5))};
}

/// `(2 * A + B) / 3`, truncating, per channel -- the specification's own
/// four-color-mode "one third of the way from `RGB1` to `RGB0`"
/// interpolation formula. Deliberately plain truncating integer
/// division, not rounded: this matches not only the specification's own
/// literal formula but also this project's own reference decoder for
/// this exact format family (`VK-GL-CTS`'s `tcuCompressedTexture.cpp`'s
/// `interpolateColor`, cross-checked directly while writing this).
RGB interpolateThird(RGB A, RGB B) {
  auto Mix = [](uint8_t A, uint8_t B) {
    return static_cast<uint8_t>((2u * A + B) / 3u);
  };
  return {Mix(A.R, B.R), Mix(A.G, B.G), Mix(A.B, B.B)};
}

/// `(A + B) / 2`, truncating, per channel -- the specification's own
/// three-color-mode midpoint formula.
RGB average(RGB A, RGB B) {
  auto Mix = [](uint8_t A, uint8_t B) {
    return static_cast<uint8_t>((unsigned(A) + B) / 2u);
  };
  return {Mix(A.R, B.R), Mix(A.G, B.G), Mix(A.B, B.B)};
}

struct BC1Header {
  RGB Color0, Color1;
  uint32_t Indices;
  /// True when this block's own 2-bit codes always mean the 4-color
  /// (opaque) interpolation, per the raw `color0`/`color1` 16-bit-word
  /// comparison. Per the specification's own prose this is
  /// `color0 > color1`; this project instead computes the logically
  /// almost-identical, but not bit-for-bit-identical-at-`color0 ==
  /// color1`, `!(color1 > color0)` -- matching `VK-GL-CTS`'s own
  /// reference decoder (`decompressBc1`'s `alphaMode`) exactly, since
  /// that decoder (not the specification's own prose) is what a real
  /// `dEQP-VK.texture.compressed_format.*` comparison actually checks
  /// against. The two conventions only disagree when `color0 ==
  /// color1`, a degenerate encoding no real encoder has a reason to
  /// produce (every codepoint would already decode to the same color
  /// either way, except the one BC1-with-alpha corner case where a
  /// texel's own index is 3 -- transparent under the specification's
  /// own prose, opaque under `VK-GL-CTS`'s convention).
  bool FourColorMode;
};

BC1Header decodeBC1Header(const uint8_t Block[8]) {
  uint32_t Color0 = static_cast<uint32_t>(loadLE(Block, 2));
  uint32_t Color1 = static_cast<uint32_t>(loadLE(Block + 2, 2));
  BC1Header H;
  H.Color0 = unpack565(Color0);
  H.Color1 = unpack565(Color1);
  H.Indices = static_cast<uint32_t>(loadLE(Block + 4, 4));
  H.FourColorMode = !(Color1 > Color0);
  return H;
}

/// The color of texel `(X, Y)` given an already-decoded BC1 header.
/// \p ForceFourColorMode overrides \p H's own mode decision to always be
/// 4-color (opaque) regardless of the block's own `color0`/`color1`
/// comparison -- BC2/BC3's own "the two code bits always use the
/// non-transparent encodings" rule for their embedded BC1-shaped color
/// block. A texel whose own index is 3 in 3-color mode decodes to
/// `{0, 0, 0}` (black); the caller (only `decodeBC1Block`, since
/// `ForceFourColorMode` is always true for BC2/BC3) is responsible for
/// deciding whether that also means transparent.
RGB bc1TexelColor(const BC1Header &H, unsigned X, unsigned Y,
                   bool ForceFourColorMode) {
  unsigned Code = getBits(H.Indices, 2 * linearIndex(X, Y), 2);
  bool FourColorMode = ForceFourColorMode || H.FourColorMode;
  switch (Code) {
  case 0:
    return H.Color0;
  case 1:
    return H.Color1;
  case 2:
    return FourColorMode ? interpolateThird(H.Color0, H.Color1)
                          : average(H.Color0, H.Color1);
  default:
    return FourColorMode ? interpolateThird(H.Color1, H.Color0)
                          : RGB{0, 0, 0};
  }
}

} // namespace

void feme::vulkan::decodeBC1Block(const uint8_t Block[8], bool HasAlpha,
                                   uint8_t *Output) {
  BC1Header H = decodeBC1Header(Block);
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      unsigned Code = getBits(H.Indices, 2 * linearIndex(X, Y), 2);
      RGB C = bc1TexelColor(H, X, Y, /*ForceFourColorMode=*/false);
      uint8_t *Texel = Output + linearIndex(X, Y) * 4;
      bool Transparent = HasAlpha && !H.FourColorMode && Code == 3;
      Texel[0] = Transparent ? 0 : C.R;
      Texel[1] = Transparent ? 0 : C.G;
      Texel[2] = Transparent ? 0 : C.B;
      Texel[3] = Transparent ? 0 : 255;
    }
  }
}

namespace {

/// Writes the BC1-shaped color half of a BC2/BC3 block (always 4-color
/// mode) into \p Output's RGB channels, leaving alpha untouched -- the
/// shared second half of both formats' own block layout.
void writeForcedFourColorRGB(const uint8_t ColorBlock[8], uint8_t *Output) {
  BC1Header H = decodeBC1Header(ColorBlock);
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      RGB C = bc1TexelColor(H, X, Y, /*ForceFourColorMode=*/true);
      uint8_t *Texel = Output + linearIndex(X, Y) * 4;
      Texel[0] = C.R;
      Texel[1] = C.G;
      Texel[2] = C.B;
    }
  }
}

} // namespace

void feme::vulkan::decodeBC2Block(const uint8_t Block[16], uint8_t *Output) {
  writeForcedFourColorRGB(Block + 8, Output);
  uint64_t AlphaBits = loadLE(Block, 8);
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      unsigned Nibble = getBits(AlphaBits, 4 * linearIndex(X, Y), 4);
      Output[linearIndex(X, Y) * 4 + 3] =
          static_cast<uint8_t>((Nibble << 4) | Nibble);
    }
  }
}

namespace {

/// Builds the 8-entry interpolated lookup table shared by BC3's alpha
/// channel and BC4/BC5 (`Endpoint0`/`Endpoint1`, each already an 8-bit
/// magnitude in `[0, 255]` for the unsigned case, or a signed `[-127,
/// 127]` value re-based to an unsigned `[0, 254]` "distance from -127"
/// domain by the caller for the signed case -- see
/// `decodeBC4Block`/`decodeBC5Block`'s own comments): 8 values if
/// `Endpoint0 > Endpoint1` (a smooth 7-step ramp), or 6 interpolated
/// values plus the two fixed extremes (`Min`/`Max`) otherwise. Every
/// division here is plain truncating integer division, matching this
/// format family's own reference decoder
/// (`tcuCompressedTexture.cpp`'s `decompressBc3`/`decompressBc4`)
/// exactly for the truncating-division *shape* of the formula -- that
/// reference decoder itself works in `float` for BC4/BC5 specifically,
/// so this integer table's own final-bit rounding is not guaranteed
/// bit-identical to it; see `BCDecode.h`'s own file comment on why that
/// discrepancy is acceptable for this still-unwired slice.
void buildInterpolatedTable(int Endpoint0, int Endpoint1, int Min, int Max,
                             int Table[8]) {
  Table[0] = Endpoint0;
  Table[1] = Endpoint1;
  if (Endpoint0 > Endpoint1) {
    for (int I = 0; I != 6; ++I)
      Table[I + 2] = (Endpoint0 * (6 - I) + Endpoint1 * (1 + I)) / 7;
  } else {
    for (int I = 0; I != 4; ++I)
      Table[I + 2] = (Endpoint0 * (4 - I) + Endpoint1 * (1 + I)) / 5;
    Table[6] = Min;
    Table[7] = Max;
  }
}

/// The single-channel BC4-shaped decode shared by BC3's own alpha
/// channel, standalone BC4, and BC5's own two independent sub-blocks:
/// an `endpoint0`/`endpoint1` byte pair followed by 16 packed 3-bit
/// codes (bits 16..63 of the 8-byte sub-block, little-endian, pixel
/// `linearIndex(X, Y)`'s own code at bit `3 * linearIndex(X, Y)` of that
/// 48-bit field). \p Signed selects the SNORM interpretation
/// (`Endpoint0`/`Endpoint1` read as signed 8-bit two's complement, an
/// endpoint byte of exactly -128 treated as -127 per the specification's
/// own explicit rule, and the two fixed extremes -127/127 rather than
/// 0/255); returns one interpolated value in `[0, 255]` (unsigned) or
/// `[-127, 127]` (signed, as an `int` -- the caller narrows to
/// `int8_t`/`uint8_t`).
int bc4TexelValue(const uint8_t SubBlock[8], bool Signed, unsigned X,
                   unsigned Y) {
  int Endpoint0, Endpoint1, Min, Max;
  if (Signed) {
    auto ReadSigned = [](uint8_t B) {
      int8_t S = static_cast<int8_t>(B);
      // Not a valid encoder output per the specification, but a decoder
      // must still not misbehave on it -- treated as -127, per the
      // specification's own explicit rule (which happens to already
      // make -127 and -128 both map to the same -1.0 endpoint value,
      // so this clamp changes nothing else about the formula below).
      return S == -128 ? -127 : static_cast<int>(S);
    };
    Endpoint0 = ReadSigned(SubBlock[0]);
    Endpoint1 = ReadSigned(SubBlock[1]);
    Min = -127;
    Max = 127;
  } else {
    Endpoint0 = SubBlock[0];
    Endpoint1 = SubBlock[1];
    Min = 0;
    Max = 255;
  }
  int Table[8];
  buildInterpolatedTable(Endpoint0, Endpoint1, Min, Max, Table);
  uint64_t Bits = loadLE(SubBlock, 8) >> 16;
  unsigned Code = getBits(Bits, 3 * linearIndex(X, Y), 3);
  return Table[Code];
}

} // namespace

void feme::vulkan::decodeBC3Block(const uint8_t Block[16], uint8_t *Output) {
  writeForcedFourColorRGB(Block + 8, Output);
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      int Alpha = bc4TexelValue(Block, /*Signed=*/false, X, Y);
      Output[linearIndex(X, Y) * 4 + 3] = static_cast<uint8_t>(Alpha);
    }
  }
}

void feme::vulkan::decodeBC4Block(const uint8_t Block[8], bool Signed,
                                   uint8_t *Output) {
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      int V = bc4TexelValue(Block, Signed, X, Y);
      Output[linearIndex(X, Y)] = static_cast<uint8_t>(
          Signed ? static_cast<int8_t>(V) : static_cast<uint8_t>(V));
    }
  }
}

void feme::vulkan::decodeBC5Block(const uint8_t Block[16], bool Signed,
                                   uint8_t *Output) {
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      int R = bc4TexelValue(Block, Signed, X, Y);
      int G = bc4TexelValue(Block + 8, Signed, X, Y);
      uint8_t *Texel = Output + linearIndex(X, Y) * 2;
      Texel[0] = Signed ? static_cast<uint8_t>(static_cast<int8_t>(R))
                         : static_cast<uint8_t>(R);
      Texel[1] = Signed ? static_cast<uint8_t>(static_cast<int8_t>(G))
                         : static_cast<uint8_t>(G);
    }
  }
}
