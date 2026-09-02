//===- ETC2Decode.cpp - ETC2/EAC block decoder ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ETC2Decode.h"

#include "llvm/Support/ErrorHandling.h"

#include <algorithm>
#include <cstdint>

using namespace feme::vulkan;

namespace {

//===----------------------------------------------------------------------===//
// Bit extraction. Both an ETC1/ETC2 block and an EAC block are 64 bits,
// with the specification's own "int64bit" numbering: byte 0 (the first
// byte in memory) occupies bits 63..56, byte 7 (the last) bits 7..0 --
// i.e. `Start`/`Len` below name a field exactly as the specification's
// own bit-range tables do (e.g. "bits 63..60").
//===----------------------------------------------------------------------===//

uint64_t loadBlock64(const uint8_t Block[8]) {
  uint64_t V = 0;
  for (unsigned I = 0; I != 8; ++I)
    V = (V << 8) | Block[I];
  return V;
}

/// Bits `[Start, Start + Len)` of \p V, as an unsigned integer with bit 0
/// of the result equal to bit `Start` of \p V.
uint32_t getBits(uint64_t V, unsigned Start, unsigned Len) {
  if (Len == 0)
    return 0;
  return static_cast<uint32_t>((V >> Start) & ((uint64_t(1) << Len) - 1));
}

/// Sign-extends the low \p Bits bits of \p V (a two's-complement value)
/// to a full-width `int32_t`.
int32_t signExtend(uint32_t V, unsigned Bits) {
  uint32_t SignBit = 1u << (Bits - 1);
  return static_cast<int32_t>((V ^ SignBit) - SignBit);
}

uint8_t clamp255(int V) {
  return static_cast<uint8_t>(std::clamp(V, 0, 255));
}

// "extendNto8bits": replicates an N-bit value's own top (8 - N) bits into
// the low (8 - N) bits of an 8-bit result, per the specification's own
// `extend4to8bits`/`extend5to8bits`/etc. helpers.
uint8_t extend4to8(uint32_t V) { return static_cast<uint8_t>((V << 4) | V); }
uint8_t extend5to8(uint32_t V) {
  return static_cast<uint8_t>((V << 3) | (V >> 2));
}
uint8_t extend6to8(uint32_t V) {
  return static_cast<uint8_t>((V << 2) | (V >> 4));
}
uint8_t extend7to8(uint32_t V) {
  return static_cast<uint8_t>((V << 1) | (V >> 6));
}

/// The eight ETC1/ETC2 intensity-modifier tables ("table codeword" 0-7)
/// `individual`/`differential` mode's own two subblocks select between,
/// each already listed in the specification's own `[-b, -a, +a, +b]`
/// column order (`Table-etc2-modifiers`).
constexpr int16_t ModifierTable[8][4] = {
    {-8, -2, 2, 8},       {-17, -5, 5, 17},    {-29, -9, 9, 29},
    {-42, -13, 13, 42},   {-60, -18, 18, 60},  {-80, -24, 24, 80},
    {-106, -33, 33, 106}, {-183, -47, 47, 183},
};

/// The same eight tables, but with the two "small" entries (index 1, 2)
/// zeroed -- the punchthrough-alpha format's own alternate table, used
/// for a `differential`-mode block whenever its own opaque bit is unset
/// (`Table-etc2punch-modifiers-b`).
constexpr int16_t PunchthroughModifierTable[8][4] = {
    {-8, 0, 0, 8},     {-17, 0, 0, 17},   {-29, 0, 0, 29},  {-42, 0, 0, 42},
    {-60, 0, 0, 60},   {-80, 0, 0, 80},   {-106, 0, 0, 106}, {-183, 0, 0, 183},
};

/// The ETC1/ETC2 "distance" lookup table `T`/`H` mode's own 3-bit index
/// selects into (`Table-etc2-distancetable`).
constexpr int DistanceTable[8] = {3, 6, 11, 16, 23, 32, 41, 64};

/// Maps a raw 2-bit pixel-index value (`(MSB << 1) | LSB`) to the
/// modifier-table row actually selected -- the specification's own
/// mapping is *not* the identity (`Table-etc2-pixelindices`): raw 0 (00)
/// selects the table's "+a" (small positive) entry, row 2; raw 1 (01)
/// selects "+b" (large positive), row 3; raw 2 (10) selects "-a" (small
/// negative), row 1; raw 3 (11) selects "-b" (large negative), row 0.
/// Only `individual`/`differential` mode uses this remapping; `T`/`H`
/// mode instead use the raw value directly as a paint-color index (see
/// their own callers below).
constexpr int PixelIndexToModifierRow[4] = {2, 3, 1, 0};

struct RGB {
  uint8_t R, G, B;
};

RGB addDistance(RGB C, int D) {
  return {clamp255(C.R + D), clamp255(C.G + D), clamp255(C.B + D)};
}

/// Which subblock (0 or 1) pixel `(X, Y)` belongs to, per the block's own
/// flip bit: flip=0 splits the block into left (subblock 0)/right
/// (subblock 1) 2x4 halves; flip=1 splits it into top (subblock 0)/
/// bottom (subblock 1) 4x2 halves.
unsigned subblockOf(unsigned X, unsigned Y, bool Flip) {
  return Flip ? (Y >= 2) : (X >= 2);
}

/// A texel's raw 2-bit pixel-index value (`(MSB << 1) | LSB`), read from
/// the specification's own "more/less significant pixel index bits"
/// halves (bits 31..16 and 15..0 of \p V respectively) at bit position
/// `X * 4 + Y` within each half -- the specification's own pixel-letter
/// convention (`a`..`p`, column-major: `a`..`d` is column `X=0`, `Y=0..3`)
/// numbers pixels in exactly this `X * 4 + Y` order.
unsigned rawPixelIndex(uint64_t V, unsigned X, unsigned Y) {
  unsigned Pix = X * 4 + Y;
  unsigned Msb = getBits(V, 16 + Pix, 1);
  unsigned Lsb = getBits(V, Pix, 1);
  return (Msb << 1) | Lsb;
}

enum class Mode { Individual, Differential, T, H, Planar };

/// Determines a block's own mode from the shared mode-selection field
/// (`R`/`Rd`/`G`/`Gd`/`B`/`Bd`, bits 63..40, at identical bit positions
/// regardless of which mode is ultimately selected) plus, for the
/// `individual`-mode-capable (non-punchthrough) format only, the
/// differential bit (bit 33) -- mirrors the specification's own "First,
/// R and Rd are added..." algorithm exactly. \p HasIndividualMode is
/// false for the punchthrough-alpha format, which has no `individual`
/// mode at all (its would-be differential bit is instead an opaque
/// flag, entirely unrelated to mode selection): mode there is purely the
/// overflow test below, per the specification's own punchthrough-alpha
/// mode-selection text.
Mode selectMode(uint64_t V, bool HasIndividualMode) {
  if (HasIndividualMode && getBits(V, 33, 1) == 0)
    return Mode::Individual;
  int R = static_cast<int>(getBits(V, 59, 5));
  int Rd = signExtend(getBits(V, 56, 3), 3);
  if (R + Rd < 0 || R + Rd > 31)
    return Mode::T;
  int G = static_cast<int>(getBits(V, 51, 5));
  int Gd = signExtend(getBits(V, 48, 3), 3);
  if (G + Gd < 0 || G + Gd > 31)
    return Mode::H;
  int B = static_cast<int>(getBits(V, 43, 5));
  int Bd = signExtend(getBits(V, 40, 3), 3);
  if (B + Bd < 0 || B + Bd > 31)
    return Mode::Planar;
  return Mode::Differential;
}

struct IndivDiffFields {
  RGB Base[2];
  int Table[2];
  bool Flip;
};

/// Decodes the `individual`/`differential`-mode header fields (bits
/// 63..32): two base colors, two modifier-table indices, and the flip
/// bit. Both modes share the table-index/flip-bit positions; only the
/// base-color encoding (4-bit-per-channel-times-two vs. 5-bit-plus-3-bit-
/// delta) differs, per `ETC2IndividualLayout`/`ETC2DifferentialLayout`.
IndivDiffFields decodeIndivDiff(uint64_t V, bool Individual) {
  IndivDiffFields F;
  F.Flip = getBits(V, 32, 1) != 0;
  F.Table[0] = static_cast<int>(getBits(V, 37, 3));
  F.Table[1] = static_cast<int>(getBits(V, 34, 3));
  if (Individual) {
    F.Base[0] = {extend4to8(getBits(V, 60, 4)), extend4to8(getBits(V, 52, 4)),
                 extend4to8(getBits(V, 44, 4))};
    F.Base[1] = {extend4to8(getBits(V, 56, 4)), extend4to8(getBits(V, 48, 4)),
                 extend4to8(getBits(V, 40, 4))};
  } else {
    int R1 = static_cast<int>(getBits(V, 59, 5));
    int G1 = static_cast<int>(getBits(V, 51, 5));
    int B1 = static_cast<int>(getBits(V, 43, 5));
    int Rd = signExtend(getBits(V, 56, 3), 3);
    int Gd = signExtend(getBits(V, 48, 3), 3);
    int Bd = signExtend(getBits(V, 40, 3), 3);
    F.Base[0] = {extend5to8(R1), extend5to8(G1), extend5to8(B1)};
    F.Base[1] = {extend5to8(R1 + Rd), extend5to8(G1 + Gd),
                 extend5to8(B1 + Bd)};
  }
  return F;
}

struct DistanceModeFields {
  RGB Base[2];
  int Distance;
};

/// Decodes `T` mode's own header fields (`ETC2TLayout`): two 4-bit-per-
/// channel base colors stored in the specification's own scattered bit
/// positions (unlike `individual` mode's sequential ones), plus a 3-bit
/// distance-table index split across two non-adjacent fields (`da`, 2
/// bits; `db`, 1 bit).
DistanceModeFields decodeTMode(uint64_t V) {
  unsigned R1 = (getBits(V, 59, 2) << 2) | getBits(V, 56, 2);
  unsigned G1 = getBits(V, 52, 4);
  unsigned B1 = getBits(V, 48, 4);
  unsigned R2 = getBits(V, 44, 4);
  unsigned G2 = getBits(V, 40, 4);
  unsigned B2 = getBits(V, 36, 4);
  unsigned Da = getBits(V, 34, 2);
  unsigned Db = getBits(V, 32, 1);
  return {{{extend4to8(R1), extend4to8(G1), extend4to8(B1)},
           {extend4to8(R2), extend4to8(G2), extend4to8(B2)}},
          DistanceTable[(Da << 1) | Db]};
}

/// Decodes `H` mode's own header fields (`ETC2HLayout`): two 4-bit-per-
/// channel base colors, again scattered, plus a 3-bit distance-table
/// index whose most/middle bits (`da`, `db`) are stored explicitly but
/// whose least-significant bit is instead *computed*, as
/// `base color 1 >= base color 2` (numerically, as an `R:G:B` 24-bit
/// packed value) -- the specification's own tie-breaking rule that lets
/// an encoder choose which of two otherwise-identical bit patterns
/// applies.
DistanceModeFields decodeHMode(uint64_t V) {
  unsigned R1 = getBits(V, 59, 4);
  unsigned G1 = (getBits(V, 56, 3) << 1) | getBits(V, 52, 1);
  unsigned B1 = (getBits(V, 51, 1) << 3) | getBits(V, 47, 3);
  unsigned R2 = getBits(V, 43, 4);
  unsigned G2 = getBits(V, 39, 4);
  unsigned B2 = getBits(V, 35, 4);
  unsigned Da = getBits(V, 34, 1);
  unsigned Db = getBits(V, 32, 1);
  RGB Base0{extend4to8(R1), extend4to8(G1), extend4to8(B1)};
  RGB Base1{extend4to8(R2), extend4to8(G2), extend4to8(B2)};
  auto PackedValue = [](RGB C) {
    return (unsigned(C.R) << 16) | (unsigned(C.G) << 8) | C.B;
  };
  unsigned Cmp = PackedValue(Base0) >= PackedValue(Base1) ? 1u : 0u;
  return {{Base0, Base1}, DistanceTable[(Da << 2) | (Db << 1) | Cmp]};
}

struct PlanarFields {
  RGB Origin, Horiz, Vert;
};

/// Decodes `planar` mode's own header fields (`ETC2Planar`): three
/// RGB:676-format base colors (`origin`, `horizontal`, `vertical`), each
/// with some channels split across non-consecutive bit ranges per the
/// specification's own table.
PlanarFields decodePlanar(uint64_t V) {
  unsigned R = getBits(V, 57, 6);
  unsigned G = (getBits(V, 56, 1) << 6) | getBits(V, 49, 6);
  unsigned B = (getBits(V, 48, 1) << 5) | (getBits(V, 43, 2) << 3) |
               getBits(V, 39, 3);
  unsigned Rh = (getBits(V, 34, 5) << 1) | getBits(V, 32, 1);
  unsigned Gh = getBits(V, 25, 7);
  unsigned Bh = getBits(V, 19, 6);
  unsigned Rv = getBits(V, 13, 6);
  unsigned Gv = getBits(V, 6, 7);
  unsigned Bv = getBits(V, 0, 6);
  return {{extend6to8(R), extend7to8(G), extend6to8(B)},
          {extend6to8(Rh), extend7to8(Gh), extend6to8(Bh)},
          {extend6to8(Rv), extend7to8(Gv), extend6to8(Bv)}};
}

/// The `planar`-mode color of pixel `(X, Y)` (`X`, `Y` in `[0, 3]`),
/// per the specification's own bilinear-plane formula, expressed (as the
/// specification itself notes is equivalent) with only integer
/// arithmetic: `clamp255((X * (H - O) + Y * (V - O) + 4 * O + 2) >> 2)`.
RGB planarTexel(const PlanarFields &P, int X, int Y) {
  auto Interp = [&](int O, int H, int V) {
    return clamp255((X * (H - O) + Y * (V - O) + 4 * O + 2) >> 2);
  };
  return {static_cast<uint8_t>(Interp(P.Origin.R, P.Horiz.R, P.Vert.R)),
          static_cast<uint8_t>(Interp(P.Origin.G, P.Horiz.G, P.Vert.G)),
          static_cast<uint8_t>(Interp(P.Origin.B, P.Horiz.B, P.Vert.B))};
}

/// The color of pixel `(X, Y)` in a block already known to be in mode
/// \p M, decoding that mode's own header fields fresh for each call --
/// this is only ever called 16 times per 64-bit block, so the header-
/// field redundancy this implies is a deliberate, harmless
/// simplicity-over-micro-optimization tradeoff, the same one this
/// software renderer's other per-texel decode paths already make.
/// \p UsePunchthroughTables selects the punchthrough-alpha format's own
/// alternate `differential`-mode modifier tables (`Table-etc2punch-
/// modifiers-b`), used when that format's own opaque bit is unset; it
/// has no effect on `individual`, `T`, `H`, or `planar` mode, none of
/// which the specification varies by the opaque bit for color purposes.
RGB colorFor(uint64_t V, Mode M, unsigned X, unsigned Y,
             bool UsePunchthroughTables) {
  unsigned Raw = rawPixelIndex(V, X, Y);
  switch (M) {
  case Mode::Individual:
  case Mode::Differential: {
    IndivDiffFields F = decodeIndivDiff(V, M == Mode::Individual);
    unsigned Sub = subblockOf(X, Y, F.Flip);
    const auto &Table =
        UsePunchthroughTables ? PunchthroughModifierTable : ModifierTable;
    int Modifier = Table[F.Table[Sub]][PixelIndexToModifierRow[Raw]];
    return addDistance(F.Base[Sub], Modifier);
  }
  case Mode::T: {
    DistanceModeFields F = decodeTMode(V);
    switch (Raw) {
    case 0:
      return F.Base[0];
    case 1:
      return addDistance(F.Base[1], F.Distance);
    case 2:
      return F.Base[1];
    default:
      return addDistance(F.Base[1], -F.Distance);
    }
  }
  case Mode::H: {
    DistanceModeFields F = decodeHMode(V);
    switch (Raw) {
    case 0:
      return addDistance(F.Base[0], F.Distance);
    case 1:
      return addDistance(F.Base[0], -F.Distance);
    case 2:
      return addDistance(F.Base[1], F.Distance);
    default:
      return addDistance(F.Base[1], -F.Distance);
    }
  }
  case Mode::Planar:
    return planarTexel(decodePlanar(V), static_cast<int>(X),
                        static_cast<int>(Y));
  }
  llvm_unreachable("all ETC2 Mode enumerators handled above");
}

} // namespace

void feme::vulkan::decodeETC2Block(const uint8_t Block[8], uint8_t *Output) {
  uint64_t V = loadBlock64(Block);
  Mode M = selectMode(V, /*HasIndividualMode=*/true);
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      RGB C = colorFor(V, M, X, Y, /*UsePunchthroughTables=*/false);
      uint8_t *Texel = Output + (Y * 4 + X) * 4;
      Texel[0] = C.R;
      Texel[1] = C.G;
      Texel[2] = C.B;
      Texel[3] = 255;
    }
  }
}

void feme::vulkan::decodeETC2PunchthroughAlphaBlock(const uint8_t Block[8],
                                                     uint8_t *Output) {
  uint64_t V = loadBlock64(Block);
  bool Opaque = getBits(V, 33, 1) != 0;
  Mode M = selectMode(V, /*HasIndividualMode=*/false);
  // The specification requires a valid encoder to never produce an unset
  // opaque bit for a `planar`-mode block, but also requires a decoder to
  // still treat one as opaque if it occurs -- see this file's own header
  // comment on `decodeETC2PunchthroughAlphaBlock` for why `planar` mode
  // has no transparency of its own regardless.
  if (M == Mode::Planar)
    Opaque = true;
  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      uint8_t *Texel = Output + (Y * 4 + X) * 4;
      // Raw pixel-index 2 (MSB=1, LSB=0) is the specification's own
      // reserved "transparent" index whenever the block is not opaque.
      if (!Opaque && rawPixelIndex(V, X, Y) == 2) {
        Texel[0] = Texel[1] = Texel[2] = Texel[3] = 0;
        continue;
      }
      RGB C = colorFor(V, M, X, Y, /*UsePunchthroughTables=*/!Opaque);
      Texel[0] = C.R;
      Texel[1] = C.G;
      Texel[2] = C.B;
      Texel[3] = 255;
    }
  }
}

namespace {

/// The 16 EAC intensity-modifier tables ("table index" 0-15) shared by
/// the alpha channel of RGBA ETC2 and by R11/RG11 EAC (`Table-etc2eac-
/// modifiers`).
constexpr int8_t EACModifierTable[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},   {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},   {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},   {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},   {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},    {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},    {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},    {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},     {-3, -5, -7, -9, 2, 4, 6, 8},
};

/// The EAC per-pixel index for texel `(X, Y)`, read from \p V's own bits
/// 47..0: 16 sequential 3-bit fields, pixel `a` (`X=0, Y=0`) in the
/// most-significant field (bits 47..45) and pixel `p` (`X=3, Y=3`) in the
/// least-significant (bits 2..0) -- i.e. field `Pix` (`Pix = X * 4 + Y`,
/// this file's shared pixel-letter convention) occupies bits
/// `[45 - 3 * Pix, 48 - 3 * Pix)` (`Table-etc2eac-dataformat` part b,
/// noting its own "stored in a..p order... with each bit of each alpha
/// index stored consecutively" -- unlike the RGB pixel-index halves
/// above, which interleave MSBs and LSBs into two separate 16-bit
/// halves).
unsigned eacPixelIndex(uint64_t V, unsigned X, unsigned Y) {
  unsigned Pix = X * 4 + Y;
  return getBits(V, 45 - 3 * Pix, 3);
}

} // namespace

void feme::vulkan::decodeEACBlock(const uint8_t Block[8], bool Signed,
                                   uint16_t *Output) {
  uint64_t V = loadBlock64(Block);
  unsigned BaseRaw = getBits(V, 56, 8);
  unsigned Multiplier = getBits(V, 52, 4);
  unsigned TableIdx = getBits(V, 48, 4);
  // A multiplier of 0 is specified as "treat the modifier*multiplier
  // term as modifier*1" (an unscaled add of the bare table value), *not*
  // as multiplying by 0 -- see "Unsigned/Signed R11 EAC simpler".
  bool MultiplierIsZero = Multiplier == 0;

  int SignedBase = 0;
  if (Signed) {
    SignedBase = signExtend(BaseRaw, 8);
    // -128 is not a valid encoder output for this 8-bit two's-complement
    // field (the specification restricts it to [-127, 127]), but a
    // decoder must still not misbehave on it -- treated as -127, per the
    // specification's own explicit rule for this case.
    if (SignedBase == -128)
      SignedBase = -127;
  }

  for (unsigned Y = 0; Y != 4; ++Y) {
    for (unsigned X = 0; X != 4; ++X) {
      unsigned PixIdx = eacPixelIndex(V, X, Y);
      int Modifier = EACModifierTable[TableIdx][PixIdx];
      int Term = MultiplierIsZero
                     ? Modifier
                     : Modifier * static_cast<int>(Multiplier) * 8;
      int Value11;
      uint16_t Result16;
      if (Signed) {
        Value11 = std::clamp(SignedBase * 8 + Term, -1023, 1023);
        bool Neg = Value11 < 0;
        unsigned Mag = static_cast<unsigned>(Neg ? -Value11 : Value11);
        // Signed extension replicates the top bits with a *5*-bit shift
        // (not unsigned's 6-bit one) per the specification's own worked
        // example (470 -> 15054) -- confirmed by direct arithmetic
        // cross-check against that example while writing this.
        auto MagExt = static_cast<uint16_t>((Mag << 5) + (Mag >> 5));
        Result16 = Neg ? static_cast<uint16_t>(-static_cast<int32_t>(MagExt))
                        : MagExt;
      } else {
        Value11 =
            std::clamp(static_cast<int>(BaseRaw) * 8 + 4 + Term, 0, 2047);
        auto UValue11 = static_cast<unsigned>(Value11);
        Result16 = static_cast<uint16_t>((UValue11 << 5) + (UValue11 >> 6));
      }
      Output[Y * 4 + X] = Result16;
    }
  }
}
