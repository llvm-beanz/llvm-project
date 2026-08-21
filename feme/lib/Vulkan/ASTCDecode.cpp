//===- ASTCDecode.cpp - ASTC LDR block decoder ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTCDecode.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

using namespace feme::vulkan;

namespace {

//===----------------------------------------------------------------------===//
// Bit extraction. An ASTC block is 128 bits, bit 0 the LSB of byte 0
// ("class ASTC" numbering the spec itself uses); everything below reads a
// closed sub-range of it, never the whole thing at once, so two 64-bit
// halves are all the storage this needs.
//===----------------------------------------------------------------------===//

struct Block128 {
  uint64_t Lo = 0;
  uint64_t Hi = 0;
};

Block128 loadBlock(const uint8_t Block[16]) {
  Block128 V;
  for (unsigned I = 0; I != 8; ++I)
    V.Lo |= uint64_t(Block[I]) << (8 * I);
  for (unsigned I = 0; I != 8; ++I)
    V.Hi |= uint64_t(Block[8 + I]) << (8 * I);
  return V;
}

/// Bits `[Start, Start + Len)` of \p V, as an unsigned integer with bit 0
/// of the result equal to bit `Start` of \p V. `Len` must be in `[0, 32]`
/// and `Start + Len` in `[0, 128]` -- every field this decoder reads fits
/// well within both bounds.
uint32_t getBits(const Block128 &V, unsigned Start, unsigned Len) {
  if (Len == 0)
    return 0;
  uint64_t Result;
  if (Start >= 64)
    Result = V.Hi >> (Start - 64);
  else if (Start + Len <= 64)
    Result = V.Lo >> Start;
  else
    Result = (V.Lo >> Start) | (V.Hi << (64 - Start));
  return static_cast<uint32_t>(Result & ((uint64_t(1) << Len) - 1));
}

/// Reverses the low \p N bits of \p X (bit 0 <-> bit `N - 1`, etc.).
uint32_t reverseBitsN(uint32_t X, unsigned N) {
  uint32_t Result = 0;
  for (unsigned I = 0; I != N; ++I)
    Result |= ((X >> I) & 1u) << (N - 1 - I);
  return Result;
}

/// The ASTC weight grid is stored starting from bit 127 and growing
/// *downward*, i.e. read as a normal bitstream from the high end of the
/// block but with each field's bits in the opposite order from
/// `getBits`'s convention -- equivalent to reading `Len` bits starting at
/// `Start` from the full 128-bit-reversed block, which is exactly what
/// this computes without ever materializing that reversal.
uint32_t getBitsFromEnd(const Block128 &V, unsigned Start, unsigned Len) {
  return reverseBitsN(getBits(V, 128 - Start - Len, Len), Len);
}

//===----------------------------------------------------------------------===//
// Bounded Integer Sequence Encoding (BISE): trit/quint tables (ASTC
// specification section "Integer Sequence Encoding") and the range
// bookkeeping every quantized value (a weight or a color endpoint
// component) is stored with.
//===----------------------------------------------------------------------===//

/// Every trit quintuple ASTC's 8-bit-to-5-trit encoding can represent,
/// indexed by the 8-bit encoding. There is exactly one correct table (the
/// encoding is a fixed bijection the specification defines), so this is
/// the same table any conformant ASTC decoder computes or embeds.
constexpr std::array<std::array<uint8_t, 5>, 256> TritEncodings = {{
    {0, 0, 0, 0, 0}, {1, 0, 0, 0, 0}, {2, 0, 0, 0, 0}, {0, 0, 2, 0, 0},
    {0, 1, 0, 0, 0}, {1, 1, 0, 0, 0}, {2, 1, 0, 0, 0}, {1, 0, 2, 0, 0},
    {0, 2, 0, 0, 0}, {1, 2, 0, 0, 0}, {2, 2, 0, 0, 0}, {2, 0, 2, 0, 0},
    {0, 2, 2, 0, 0}, {1, 2, 2, 0, 0}, {2, 2, 2, 0, 0}, {2, 0, 2, 0, 0},
    {0, 0, 1, 0, 0}, {1, 0, 1, 0, 0}, {2, 0, 1, 0, 0}, {0, 1, 2, 0, 0},
    {0, 1, 1, 0, 0}, {1, 1, 1, 0, 0}, {2, 1, 1, 0, 0}, {1, 1, 2, 0, 0},
    {0, 2, 1, 0, 0}, {1, 2, 1, 0, 0}, {2, 2, 1, 0, 0}, {2, 1, 2, 0, 0},
    {0, 0, 0, 2, 2}, {1, 0, 0, 2, 2}, {2, 0, 0, 2, 2}, {0, 0, 2, 2, 2},
    {0, 0, 0, 1, 0}, {1, 0, 0, 1, 0}, {2, 0, 0, 1, 0}, {0, 0, 2, 1, 0},
    {0, 1, 0, 1, 0}, {1, 1, 0, 1, 0}, {2, 1, 0, 1, 0}, {1, 0, 2, 1, 0},
    {0, 2, 0, 1, 0}, {1, 2, 0, 1, 0}, {2, 2, 0, 1, 0}, {2, 0, 2, 1, 0},
    {0, 2, 2, 1, 0}, {1, 2, 2, 1, 0}, {2, 2, 2, 1, 0}, {2, 0, 2, 1, 0},
    {0, 0, 1, 1, 0}, {1, 0, 1, 1, 0}, {2, 0, 1, 1, 0}, {0, 1, 2, 1, 0},
    {0, 1, 1, 1, 0}, {1, 1, 1, 1, 0}, {2, 1, 1, 1, 0}, {1, 1, 2, 1, 0},
    {0, 2, 1, 1, 0}, {1, 2, 1, 1, 0}, {2, 2, 1, 1, 0}, {2, 1, 2, 1, 0},
    {0, 1, 0, 2, 2}, {1, 1, 0, 2, 2}, {2, 1, 0, 2, 2}, {1, 0, 2, 2, 2},
    {0, 0, 0, 2, 0}, {1, 0, 0, 2, 0}, {2, 0, 0, 2, 0}, {0, 0, 2, 2, 0},
    {0, 1, 0, 2, 0}, {1, 1, 0, 2, 0}, {2, 1, 0, 2, 0}, {1, 0, 2, 2, 0},
    {0, 2, 0, 2, 0}, {1, 2, 0, 2, 0}, {2, 2, 0, 2, 0}, {2, 0, 2, 2, 0},
    {0, 2, 2, 2, 0}, {1, 2, 2, 2, 0}, {2, 2, 2, 2, 0}, {2, 0, 2, 2, 0},
    {0, 0, 1, 2, 0}, {1, 0, 1, 2, 0}, {2, 0, 1, 2, 0}, {0, 1, 2, 2, 0},
    {0, 1, 1, 2, 0}, {1, 1, 1, 2, 0}, {2, 1, 1, 2, 0}, {1, 1, 2, 2, 0},
    {0, 2, 1, 2, 0}, {1, 2, 1, 2, 0}, {2, 2, 1, 2, 0}, {2, 1, 2, 2, 0},
    {0, 2, 0, 2, 2}, {1, 2, 0, 2, 2}, {2, 2, 0, 2, 2}, {2, 0, 2, 2, 2},
    {0, 0, 0, 0, 2}, {1, 0, 0, 0, 2}, {2, 0, 0, 0, 2}, {0, 0, 2, 0, 2},
    {0, 1, 0, 0, 2}, {1, 1, 0, 0, 2}, {2, 1, 0, 0, 2}, {1, 0, 2, 0, 2},
    {0, 2, 0, 0, 2}, {1, 2, 0, 0, 2}, {2, 2, 0, 0, 2}, {2, 0, 2, 0, 2},
    {0, 2, 2, 0, 2}, {1, 2, 2, 0, 2}, {2, 2, 2, 0, 2}, {2, 0, 2, 0, 2},
    {0, 0, 1, 0, 2}, {1, 0, 1, 0, 2}, {2, 0, 1, 0, 2}, {0, 1, 2, 0, 2},
    {0, 1, 1, 0, 2}, {1, 1, 1, 0, 2}, {2, 1, 1, 0, 2}, {1, 1, 2, 0, 2},
    {0, 2, 1, 0, 2}, {1, 2, 1, 0, 2}, {2, 2, 1, 0, 2}, {2, 1, 2, 0, 2},
    {0, 2, 2, 2, 2}, {1, 2, 2, 2, 2}, {2, 2, 2, 2, 2}, {2, 0, 2, 2, 2},
    {0, 0, 0, 0, 1}, {1, 0, 0, 0, 1}, {2, 0, 0, 0, 1}, {0, 0, 2, 0, 1},
    {0, 1, 0, 0, 1}, {1, 1, 0, 0, 1}, {2, 1, 0, 0, 1}, {1, 0, 2, 0, 1},
    {0, 2, 0, 0, 1}, {1, 2, 0, 0, 1}, {2, 2, 0, 0, 1}, {2, 0, 2, 0, 1},
    {0, 2, 2, 0, 1}, {1, 2, 2, 0, 1}, {2, 2, 2, 0, 1}, {2, 0, 2, 0, 1},
    {0, 0, 1, 0, 1}, {1, 0, 1, 0, 1}, {2, 0, 1, 0, 1}, {0, 1, 2, 0, 1},
    {0, 1, 1, 0, 1}, {1, 1, 1, 0, 1}, {2, 1, 1, 0, 1}, {1, 1, 2, 0, 1},
    {0, 2, 1, 0, 1}, {1, 2, 1, 0, 1}, {2, 2, 1, 0, 1}, {2, 1, 2, 0, 1},
    {0, 0, 1, 2, 2}, {1, 0, 1, 2, 2}, {2, 0, 1, 2, 2}, {0, 1, 2, 2, 2},
    {0, 0, 0, 1, 1}, {1, 0, 0, 1, 1}, {2, 0, 0, 1, 1}, {0, 0, 2, 1, 1},
    {0, 1, 0, 1, 1}, {1, 1, 0, 1, 1}, {2, 1, 0, 1, 1}, {1, 0, 2, 1, 1},
    {0, 2, 0, 1, 1}, {1, 2, 0, 1, 1}, {2, 2, 0, 1, 1}, {2, 0, 2, 1, 1},
    {0, 2, 2, 1, 1}, {1, 2, 2, 1, 1}, {2, 2, 2, 1, 1}, {2, 0, 2, 1, 1},
    {0, 0, 1, 1, 1}, {1, 0, 1, 1, 1}, {2, 0, 1, 1, 1}, {0, 1, 2, 1, 1},
    {0, 1, 1, 1, 1}, {1, 1, 1, 1, 1}, {2, 1, 1, 1, 1}, {1, 1, 2, 1, 1},
    {0, 2, 1, 1, 1}, {1, 2, 1, 1, 1}, {2, 2, 1, 1, 1}, {2, 1, 2, 1, 1},
    {0, 1, 1, 2, 2}, {1, 1, 1, 2, 2}, {2, 1, 1, 2, 2}, {1, 1, 2, 2, 2},
    {0, 0, 0, 2, 1}, {1, 0, 0, 2, 1}, {2, 0, 0, 2, 1}, {0, 0, 2, 2, 1},
    {0, 1, 0, 2, 1}, {1, 1, 0, 2, 1}, {2, 1, 0, 2, 1}, {1, 0, 2, 2, 1},
    {0, 2, 0, 2, 1}, {1, 2, 0, 2, 1}, {2, 2, 0, 2, 1}, {2, 0, 2, 2, 1},
    {0, 2, 2, 2, 1}, {1, 2, 2, 2, 1}, {2, 2, 2, 2, 1}, {2, 0, 2, 2, 1},
    {0, 0, 1, 2, 1}, {1, 0, 1, 2, 1}, {2, 0, 1, 2, 1}, {0, 1, 2, 2, 1},
    {0, 1, 1, 2, 1}, {1, 1, 1, 2, 1}, {2, 1, 1, 2, 1}, {1, 1, 2, 2, 1},
    {0, 2, 1, 2, 1}, {1, 2, 1, 2, 1}, {2, 2, 1, 2, 1}, {2, 1, 2, 2, 1},
    {0, 2, 1, 2, 2}, {1, 2, 1, 2, 2}, {2, 2, 1, 2, 2}, {2, 1, 2, 2, 2},
    {0, 0, 0, 1, 2}, {1, 0, 0, 1, 2}, {2, 0, 0, 1, 2}, {0, 0, 2, 1, 2},
    {0, 1, 0, 1, 2}, {1, 1, 0, 1, 2}, {2, 1, 0, 1, 2}, {1, 0, 2, 1, 2},
    {0, 2, 0, 1, 2}, {1, 2, 0, 1, 2}, {2, 2, 0, 1, 2}, {2, 0, 2, 1, 2},
    {0, 2, 2, 1, 2}, {1, 2, 2, 1, 2}, {2, 2, 2, 1, 2}, {2, 0, 2, 1, 2},
    {0, 0, 1, 1, 2}, {1, 0, 1, 1, 2}, {2, 0, 1, 1, 2}, {0, 1, 2, 1, 2},
    {0, 1, 1, 1, 2}, {1, 1, 1, 1, 2}, {2, 1, 1, 1, 2}, {1, 1, 2, 1, 2},
    {0, 2, 1, 1, 2}, {1, 2, 1, 1, 2}, {2, 2, 1, 1, 2}, {2, 1, 2, 1, 2},
    {0, 2, 2, 2, 2}, {1, 2, 2, 2, 2}, {2, 2, 2, 2, 2}, {2, 1, 2, 2, 2},
}};

/// Every quint triple ASTC's 7-bit-to-3-quint encoding can represent,
/// indexed by the 7-bit encoding -- see `TritEncodings`'s comment; the
/// same "one correct table" reasoning applies.
constexpr std::array<std::array<uint8_t, 3>, 128> QuintEncodings = {{
    {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}, {0, 4, 0},
    {4, 4, 0}, {4, 4, 4}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {3, 1, 0},
    {4, 1, 0}, {1, 4, 0}, {4, 4, 1}, {4, 4, 4}, {0, 2, 0}, {1, 2, 0},
    {2, 2, 0}, {3, 2, 0}, {4, 2, 0}, {2, 4, 0}, {4, 4, 2}, {4, 4, 4},
    {0, 3, 0}, {1, 3, 0}, {2, 3, 0}, {3, 3, 0}, {4, 3, 0}, {3, 4, 0},
    {4, 4, 3}, {4, 4, 4}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {3, 0, 1},
    {4, 0, 1}, {0, 4, 1}, {4, 0, 4}, {0, 4, 4}, {0, 1, 1}, {1, 1, 1},
    {2, 1, 1}, {3, 1, 1}, {4, 1, 1}, {1, 4, 1}, {4, 1, 4}, {1, 4, 4},
    {0, 2, 1}, {1, 2, 1}, {2, 2, 1}, {3, 2, 1}, {4, 2, 1}, {2, 4, 1},
    {4, 2, 4}, {2, 4, 4}, {0, 3, 1}, {1, 3, 1}, {2, 3, 1}, {3, 3, 1},
    {4, 3, 1}, {3, 4, 1}, {4, 3, 4}, {3, 4, 4}, {0, 0, 2}, {1, 0, 2},
    {2, 0, 2}, {3, 0, 2}, {4, 0, 2}, {0, 4, 2}, {2, 0, 4}, {3, 0, 4},
    {0, 1, 2}, {1, 1, 2}, {2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {1, 4, 2},
    {2, 1, 4}, {3, 1, 4}, {0, 2, 2}, {1, 2, 2}, {2, 2, 2}, {3, 2, 2},
    {4, 2, 2}, {2, 4, 2}, {2, 2, 4}, {3, 2, 4}, {0, 3, 2}, {1, 3, 2},
    {2, 3, 2}, {3, 3, 2}, {4, 3, 2}, {3, 4, 2}, {2, 3, 4}, {3, 3, 4},
    {0, 0, 3}, {1, 0, 3}, {2, 0, 3}, {3, 0, 3}, {4, 0, 3}, {0, 4, 3},
    {0, 0, 4}, {1, 0, 4}, {0, 1, 3}, {1, 1, 3}, {2, 1, 3}, {3, 1, 3},
    {4, 1, 3}, {1, 4, 3}, {0, 1, 4}, {1, 1, 4}, {0, 2, 3}, {1, 2, 3},
    {2, 2, 3}, {3, 2, 3}, {4, 2, 3}, {2, 4, 3}, {0, 2, 4}, {1, 2, 4},
    {0, 3, 3}, {1, 3, 3}, {2, 3, 3}, {3, 3, 3}, {4, 3, 3}, {3, 4, 3},
    {0, 3, 4}, {1, 3, 4},
}};

/// How many bits of a `range`-quantized integer sequence's encoding are
/// spent on trits, quints, or plain bits, per the specification's own
/// enumeration of legal ranges (each is `2^k - 1`, `3 * 2^k - 1`, or
/// `5 * 2^k - 1` for some `k`; no other value is a legal ISE range).
struct ISERangeShape {
  unsigned Trits = 0;
  unsigned Quints = 0;
  unsigned Bits = 0;
  bool Valid = false;
};

ISERangeShape iseRangeShape(unsigned Range) {
  for (unsigned K = 0; K != 9; ++K) {
    if (Range + 1 == (3u << K))
      return {1, 0, K, true};
    if (Range + 1 == (5u << K))
      return {0, 1, K, true};
    if (Range + 1 == (1u << K))
      return {0, 0, K, true};
  }
  return {0, 0, 0, false};
}

/// The total number of bits `NumVals` values quantized to `Shape` occupy,
/// per the specification's "storage requirements for a group of ISE
/// values" formula.
unsigned iseBitCount(unsigned NumVals, ISERangeShape Shape) {
  unsigned TritBits = (NumVals * 8 * Shape.Trits + 4) / 5;
  unsigned QuintBits = (NumVals * 7 * Shape.Quints + 2) / 3;
  return TritBits + QuintBits + NumVals * Shape.Bits;
}

/// Decodes `NumVals` ISE-encoded values quantized to `[0, Range]` starting
/// at bit `Start` of \p V, advancing forward (color and weight data both
/// use this direction -- see `getBitsFromEnd`'s comment for the one field
/// that instead reads from the block's high end, applied by the caller
/// before this function ever sees it).
std::vector<uint32_t> decodeISESequence(const Block128 &V, unsigned Start,
                                        unsigned NumVals, unsigned Range) {
  ISERangeShape Shape = iseRangeShape(Range);
  std::vector<uint32_t> Result;
  Result.reserve(NumVals);

  if (Shape.Trits) {
    // Interleaved trit-block bit widths (specification Table
    // "Interleaved trit block bit widths"): value `i`'s low `Shape.Bits`
    // bits are followed by this many trit-encoding bits before value
    // `i + 1`'s low bits begin.
    constexpr unsigned InterleavedBits[5] = {2, 2, 1, 2, 1};
    while (Result.size() < NumVals) {
      unsigned GroupVals = std::min<unsigned>(5, NumVals - Result.size());
      unsigned Pos = Start;
      std::array<uint32_t, 5> Low = {0, 0, 0, 0, 0};
      uint32_t Encoded = 0;
      unsigned EncodedBits = 0;
      for (unsigned I = 0; I != GroupVals; ++I) {
        Low[I] = getBits(V, Pos, Shape.Bits);
        Pos += Shape.Bits;
        uint32_t Bits = getBits(V, Pos, InterleavedBits[I]);
        Pos += InterleavedBits[I];
        Encoded |= Bits << EncodedBits;
        EncodedBits += InterleavedBits[I];
      }
      Start = Pos;
      const auto &Trits = TritEncodings[Encoded & 0xFF];
      for (unsigned I = 0; I != GroupVals; ++I)
        Result.push_back((uint32_t(Trits[I]) << Shape.Bits) | Low[I]);
    }
  } else if (Shape.Quints) {
    constexpr unsigned InterleavedBits[3] = {3, 2, 2};
    while (Result.size() < NumVals) {
      unsigned GroupVals = std::min<unsigned>(3, NumVals - Result.size());
      unsigned Pos = Start;
      std::array<uint32_t, 3> Low = {0, 0, 0};
      uint32_t Encoded = 0;
      unsigned EncodedBits = 0;
      for (unsigned I = 0; I != GroupVals; ++I) {
        Low[I] = getBits(V, Pos, Shape.Bits);
        Pos += Shape.Bits;
        uint32_t Bits = getBits(V, Pos, InterleavedBits[I]);
        Pos += InterleavedBits[I];
        Encoded |= Bits << EncodedBits;
        EncodedBits += InterleavedBits[I];
      }
      Start = Pos;
      const auto &Quints = QuintEncodings[Encoded & 0x7F];
      for (unsigned I = 0; I != GroupVals; ++I)
        Result.push_back((uint32_t(Quints[I]) << Shape.Bits) | Low[I]);
    }
  } else {
    for (unsigned I = 0; I != NumVals; ++I) {
      Result.push_back(getBits(V, Start, Shape.Bits));
      Start += Shape.Bits;
    }
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// Unquantization: turning one raw ISE-decoded value (still `[0, Range]`,
// with no correlation between its numeric value and the quantity it
// represents -- see `decodeISESequence`'s comment) into an 8-bit color
// endpoint component or a 6-bit-plus-one weight, per the specification's
// "Unquantization" section. Both share the same trit/quint arithmetic
// with different constant tables (color endpoints go through an extra
// `>> 1`-shaped step weights don't), and both fall back to plain bit
// replication when `Range` has no trit/quint component at all.
//===----------------------------------------------------------------------===//

uint32_t replicateBits(uint32_t Bits, unsigned NumBits, unsigned TotalBits) {
  uint32_t Result = Bits;
  unsigned HaveBits = NumBits;
  while (HaveBits < TotalBits) {
    unsigned Shift = std::min(NumBits, TotalBits - HaveBits);
    Result = (Result << Shift) | (Bits >> (NumBits - Shift));
    HaveBits += Shift;
  }
  return Result;
}

/// Color endpoint trit unquantization (specification "Trits and Bits" for
/// 8-bit color endpoints): `Trit` in `[0, 2]`, `Bits` the value's low
/// bits, `Range` the ISE range they were decoded at.
unsigned unquantizeTritValue(unsigned Trit, unsigned Bits, unsigned Range) {
  unsigned A = (Bits & 1) ? 0x1FF : 0;
  unsigned B = 0, C = 0;
  switch (Range) {
  case 5:
    C = 204;
    break;
  case 11: {
    unsigned X = (Bits >> 1) & 0x1;
    B = (X << 1) | (X << 2) | (X << 4) | (X << 8);
    C = 93;
    break;
  }
  case 23: {
    unsigned X = (Bits >> 1) & 0x3;
    B = X | (X << 2) | (X << 7);
    C = 44;
    break;
  }
  case 47: {
    unsigned X = (Bits >> 1) & 0x7;
    B = X | (X << 6);
    C = 22;
    break;
  }
  case 95: {
    unsigned X = (Bits >> 1) & 0xF;
    B = (X >> 2) | (X << 5);
    C = 11;
    break;
  }
  case 191: {
    unsigned X = (Bits >> 1) & 0x1F;
    B = (X >> 4) | (X << 4);
    C = 5;
    break;
  }
  default:
    break;
  }
  unsigned T = Trit * C + B;
  T ^= A;
  return (A & 0x80) | (T >> 2);
}

unsigned unquantizeQuintValue(unsigned Quint, unsigned Bits, unsigned Range) {
  unsigned A = (Bits & 1) ? 0x1FF : 0;
  unsigned B = 0, C = 0;
  switch (Range) {
  case 9:
    C = 113;
    break;
  case 19: {
    unsigned X = (Bits >> 1) & 0x1;
    B = (X << 2) | (X << 3) | (X << 8);
    C = 54;
    break;
  }
  case 39: {
    unsigned X = (Bits >> 1) & 0x3;
    B = (X >> 1) | (X << 1) | (X << 7);
    C = 26;
    break;
  }
  case 79: {
    unsigned X = (Bits >> 1) & 0x7;
    B = (X >> 1) | (X << 6);
    C = 13;
    break;
  }
  case 159: {
    unsigned X = (Bits >> 1) & 0xF;
    B = (X >> 3) | (X << 5);
    C = 6;
    break;
  }
  default:
    break;
  }
  unsigned T = Quint * C + B;
  T ^= A;
  return (A & 0x80) | (T >> 2);
}

/// Unquantizes one raw ISE value to an 8-bit (`[0, 255]`) color endpoint
/// component.
unsigned unquantizeColorValue(uint32_t Value, unsigned Range) {
  ISERangeShape Shape = iseRangeShape(Range);
  unsigned Bits = Value & ((1u << Shape.Bits) - 1);
  if (Shape.Trits)
    return unquantizeTritValue(Value >> Shape.Bits, Bits, Range);
  if (Shape.Quints)
    return unquantizeQuintValue(Value >> Shape.Bits, Bits, Range);
  return replicateBits(Bits, Shape.Bits, 8);
}

/// Weight trit/quint unquantization (specification "Trits and Bits" for
/// 6-bit weights) -- same shape as the color endpoint formulas above, but
/// with the range-2/range-4 direct cases and different constants.
unsigned unquantizeTritWeight(unsigned Trit, unsigned Bits, unsigned Range) {
  if (Range == 2)
    return (std::array<unsigned, 3>{0, 32, 63})[Trit];
  unsigned A = (Bits & 1) ? 0x7F : 0;
  unsigned B = 0, C = 0;
  switch (Range) {
  case 5:
    C = 50;
    break;
  case 11: {
    unsigned X = (Bits >> 1) & 1;
    B = X | (X << 2) | (X << 6);
    C = 23;
    break;
  }
  case 23: {
    unsigned X = (Bits >> 1) & 0x3;
    B = X | (X << 5);
    C = 11;
    break;
  }
  default:
    break;
  }
  unsigned T = Trit * C + B;
  T ^= A;
  return (A & 0x20) | (T >> 2);
}

unsigned unquantizeQuintWeight(unsigned Quint, unsigned Bits, unsigned Range) {
  if (Range == 4)
    return (std::array<unsigned, 5>{0, 16, 32, 47, 63})[Quint];
  unsigned A = (Bits & 1) ? 0x7F : 0;
  unsigned B = 0, C = 0;
  switch (Range) {
  case 9:
    C = 28;
    break;
  case 19: {
    unsigned X = (Bits >> 1) & 1;
    B = (X << 1) | (X << 6);
    C = 13;
    break;
  }
  default:
    break;
  }
  unsigned T = Quint * C + B;
  T ^= A;
  return (A & 0x20) | (T >> 2);
}

/// Unquantizes one raw ISE value to a weight in `[0, 64]` -- 65 values
/// stored in a 6-bit-or-fewer field by having every quantization skip the
/// value 33 (specification "Weight ranges are one greater..."), so every
/// caller of the trit/quint/bit-replication math below goes through this
/// single `+1`-above-32 correction.
unsigned unquantizeWeight(uint32_t Value, unsigned Range) {
  ISERangeShape Shape = iseRangeShape(Range);
  unsigned Bits = Value & ((1u << Shape.Bits) - 1);
  unsigned Raw;
  if (Shape.Trits)
    Raw = unquantizeTritWeight(Value >> Shape.Bits, Bits, Range);
  else if (Shape.Quints)
    Raw = unquantizeQuintWeight(Value >> Shape.Bits, Bits, Range);
  else
    Raw = replicateBits(Bits, Shape.Bits, 6);
  return Raw > 32 ? Raw + 1 : Raw;
}

//===----------------------------------------------------------------------===//
// Block mode: the weight grid's width, height, and ISE range, plus the
// dual-plane flag, all decoded from the block's low 11 bits (specification
// "Block Mode" table) before any of the color/weight/partition data that
// follows can be located.
//===----------------------------------------------------------------------===//

struct BlockModeInfo {
  bool Void = false;
  bool Illegal = false;
  bool DualPlane = false;
  unsigned WeightWidth = 0;
  unsigned WeightHeight = 0;
  unsigned WeightRange = 0;
};

BlockModeInfo decodeBlockMode(const Block128 &V) {
  BlockModeInfo Info;
  if (getBits(V, 0, 9) == 0x1FC) {
    Info.Void = true;
    return Info;
  }

  unsigned A, B;
  unsigned R = getBits(V, 4, 1);
  unsigned Low01 = getBits(V, 0, 2);
  if (Low01 != 0) {
    unsigned Sub = getBits(V, 2, 2);
    A = getBits(V, 5, 2);
    switch (Sub) {
    case 0: // B4_A2
      B = getBits(V, 7, 2);
      Info.WeightWidth = B + 4;
      Info.WeightHeight = A + 2;
      break;
    case 1: // B8_A2
      B = getBits(V, 7, 2);
      Info.WeightWidth = B + 8;
      Info.WeightHeight = A + 2;
      break;
    case 2: // A2_B8
      B = getBits(V, 7, 2);
      Info.WeightWidth = A + 2;
      Info.WeightHeight = B + 8;
      break;
    case 3:
      if (getBits(V, 8, 1)) { // B2_A2
        B = getBits(V, 7, 1);
        Info.WeightWidth = B + 2;
        Info.WeightHeight = A + 2;
      } else { // A2_B6
        B = getBits(V, 7, 1);
        Info.WeightWidth = A + 2;
        Info.WeightHeight = B + 6;
      }
      break;
    }
    R |= Low01 << 1;
  } else {
    unsigned Mode = getBits(V, 5, 4);
    A = getBits(V, 5, 2);
    if ((Mode & 0xC) == 0x0) {
      if (getBits(V, 0, 4) == 0) {
        Info.Illegal = true;
        return Info;
      }
      Info.WeightWidth = 12; // 12_A2
      Info.WeightHeight = A + 2;
    } else if ((Mode & 0xC) == 0x4) {
      Info.WeightWidth = A + 2; // A2_12
      Info.WeightHeight = 12;
    } else if (Mode == 0xC) {
      Info.WeightWidth = 6; // 6_10
      Info.WeightHeight = 10;
    } else if (Mode == 0xD) {
      Info.WeightWidth = 10; // 10_6
      Info.WeightHeight = 6;
    } else if ((Mode & 0xC) == 0x8) {
      B = getBits(V, 9, 2); // A6_B6
      Info.WeightWidth = A + 6;
      Info.WeightHeight = B + 6;
    } else {
      Info.Illegal = true;
      return Info;
    }
    R |= getBits(V, 2, 2) << 1;
  }

  bool IsA6B6 = Low01 == 0 && (getBits(V, 5, 4) & 0xC) == 0x8;
  unsigned H = IsA6B6 ? 0 : getBits(V, 9, 1);
  Info.DualPlane = !IsA6B6 && getBits(V, 10, 1) != 0;

  // Weight ISE range (specification Table "Range for block mode"):
  // reserved combinations (index 0, 1, 8, 9) never occur for a legal
  // (H, R) pair coming from the field layouts above.
  constexpr int WeightRanges[16] = {-1, -1, 1, 2,  3,  4,  5,  7,
                                    -1, -1, 9, 11, 15, 19, 23, 31};
  int Range = WeightRanges[(H << 3) | R];
  if (Range < 0) {
    Info.Illegal = true;
    return Info;
  }
  Info.WeightRange = static_cast<unsigned>(Range);

  unsigned NumWeights =
      Info.WeightWidth * Info.WeightHeight * (Info.DualPlane ? 2 : 1);
  if (NumWeights > 64) {
    Info.Illegal = true;
    return Info;
  }
  unsigned WeightBits =
      iseBitCount(NumWeights, iseRangeShape(Info.WeightRange));
  if (WeightBits < 24 || WeightBits > 96) {
    Info.Illegal = true;
    return Info;
  }
  return Info;
}

//===----------------------------------------------------------------------===//
// Partition selection (specification "2D Partition Selection Hash
// Function"): a procedural hash rather than a lookup table, so unlike
// every other piece of this decoder it needs no encoded-constant table.
//===----------------------------------------------------------------------===//

unsigned selectPartition(unsigned Seed, unsigned X, unsigned Y,
                         unsigned NumPartitions, unsigned NumTexels) {
  if (NumPartitions <= 1)
    return 0;

  if (NumTexels < 31) {
    X <<= 1;
    Y <<= 1;
  }

  uint32_t RNum = Seed + (NumPartitions - 1) * 1024;
  RNum ^= RNum >> 15;
  RNum -= RNum << 17;
  RNum += RNum << 7;
  RNum += RNum << 4;
  RNum ^= RNum >> 5;
  RNum += RNum << 16;
  RNum ^= RNum >> 7;
  RNum ^= RNum >> 3;
  RNum ^= RNum << 6;
  RNum ^= RNum >> 17;

  uint8_t Seed1 = RNum & 0xF, Seed2 = (RNum >> 4) & 0xF;
  uint8_t Seed3 = (RNum >> 8) & 0xF, Seed4 = (RNum >> 12) & 0xF;
  uint8_t Seed5 = (RNum >> 16) & 0xF, Seed6 = (RNum >> 20) & 0xF;
  uint8_t Seed7 = (RNum >> 24) & 0xF, Seed8 = (RNum >> 28) & 0xF;
  // The specification's seed9-12 each scale a z-coordinate term that is
  // always 0 for ASTC's 2D block partitioning, so they are omitted here
  // rather than computed and then multiplied away.

  Seed1 *= Seed1;
  Seed2 *= Seed2;
  Seed3 *= Seed3;
  Seed4 *= Seed4;
  Seed5 *= Seed5;
  Seed6 *= Seed6;
  Seed7 *= Seed7;
  Seed8 *= Seed8;

  unsigned Sh1, Sh2;
  if (Seed & 1) {
    Sh1 = (Seed & 2) ? 4 : 5;
    Sh2 = NumPartitions == 3 ? 6 : 5;
  } else {
    Sh1 = NumPartitions == 3 ? 6 : 5;
    Sh2 = (Seed & 2) ? 4 : 5;
  }

  Seed1 >>= Sh1;
  Seed2 >>= Sh2;
  Seed3 >>= Sh1;
  Seed4 >>= Sh2;
  Seed5 >>= Sh1;
  Seed6 >>= Sh2;
  Seed7 >>= Sh1;
  Seed8 >>= Sh2;

  unsigned A = Seed1 * X + Seed2 * Y + (RNum >> 14);
  unsigned B = Seed3 * X + Seed4 * Y + (RNum >> 10);
  unsigned C = Seed5 * X + Seed6 * Y + (RNum >> 6);
  unsigned D = Seed7 * X + Seed8 * Y + (RNum >> 2);

  A &= 0x3F;
  B &= 0x3F;
  C &= 0x3F;
  D &= 0x3F;

  if (NumPartitions <= 3)
    D = 0;
  if (NumPartitions <= 2)
    C = 0;

  if (A >= B && A >= C && A >= D)
    return 0;
  if (B >= C && B >= D)
    return 1;
  if (C >= D)
    return 2;
  return 3;
}

//===----------------------------------------------------------------------===//
// Weight grid infill (specification "Weight Grid Decoding"): a
// fixed-point bilinear upscale from the block's small weight grid to one
// weight per texel of the footprint.
//===----------------------------------------------------------------------===//

std::vector<unsigned> infillWeights(const std::vector<unsigned> &Grid,
                                    unsigned GridWidth, unsigned GridHeight,
                                    unsigned BlockWidth,
                                    unsigned BlockHeight) {
  auto scaleFactor = [](unsigned Dim) {
    return static_cast<unsigned>(
        (1024.0f + static_cast<float>(Dim >> 1)) / static_cast<float>(Dim - 1));
  };
  unsigned Ds = scaleFactor(BlockWidth);
  unsigned Dt = scaleFactor(BlockHeight);

  std::vector<unsigned> Result;
  Result.reserve(BlockWidth * BlockHeight);
  for (unsigned T = 0; T != BlockHeight; ++T) {
    for (unsigned S = 0; S != BlockWidth; ++S) {
      unsigned Cs = Ds * S, Ct = Dt * T;
      unsigned Gs = (Cs * (GridWidth - 1) + 32) >> 6;
      unsigned Gt = (Ct * (GridHeight - 1) + 32) >> 6;
      unsigned Js = Gs >> 4, Jt = Gt >> 4;
      unsigned Fs = Gs & 0xF, Ft = Gt & 0xF;

      unsigned W11 = (Fs * Ft + 8) >> 4;
      unsigned W01 = Ft - W11;
      unsigned W10 = Fs - W11;
      unsigned W00 = 16 - Fs - Ft + W11;

      unsigned P00 = Js + GridWidth * Jt;
      unsigned P10 = P00 + 1;
      unsigned P01 = P00 + GridWidth;
      unsigned P11 = P01 + 1;

      unsigned NumGridPts = GridWidth * GridHeight;
      unsigned Sum = 0;
      if (P00 < NumGridPts)
        Sum += Grid[P00] * W00;
      if (P10 < NumGridPts)
        Sum += Grid[P10] * W10;
      if (P01 < NumGridPts)
        Sum += Grid[P01] * W01;
      if (P11 < NumGridPts)
        Sum += Grid[P11] * W11;
      Result.push_back((Sum + 8) >> 4);
    }
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// Color endpoint modes (specification "Color Endpoint Modes"/"Endpoint
// Unquantization"): each mode's own bit-twiddling formula turning its
// unquantized color values into a pair of RGBA endpoints. Only the ten
// LDR modes are implemented (this decoder's own scope, see ASTCDecode.h);
// the six HDR-only modes fall back to opaque black via `default`.
//===----------------------------------------------------------------------===//

using RGBA = std::array<int, 4>;

int clampByte(int V) { return std::clamp(V, 0, 255); }

/// The `bit_transfer_signed` procedure: folds one bit of \p B's low end
/// into \p A's sign, shrinking both by one bit -- used by every
/// "base + signed offset" endpoint mode.
void bitTransferSigned(int &A, int &B) {
  B >>= 1;
  B |= A & 0x80;
  A >>= 1;
  A &= 0x3F;
  if (A & 0x20)
    A -= 0x40;
}

void blueContract(RGBA &C) {
  C[0] = (C[0] + C[2]) >> 1;
  C[1] = (C[1] + C[2]) >> 1;
}

unsigned numColorValuesForMode(unsigned Mode) { return (Mode / 4 + 1) * 2; }

/// Unquantizes \p Raw's first `numColorValuesForMode(Mode)` values (see
/// `decodeISESequence`) at ISE range \p Range, then decodes them per
/// color endpoint mode \p Mode (specification Table "Color Endpoint
/// Modes" for the mode numbering) into the low/high RGBA endpoint pair.
std::pair<RGBA, RGBA> decodeColorEndpoints(const std::vector<uint32_t> &Raw,
                                           unsigned Range, unsigned Mode) {
  std::array<int, 8> V{};
  for (unsigned I = 0; I != Raw.size() && I != V.size(); ++I)
    V[I] = unquantizeColorValue(Raw[I], Range);

  RGBA Lo{}, Hi{};
  switch (Mode) {
  case 0: // LDR luminance, direct.
    Lo = {V[0], V[0], V[0], 255};
    Hi = {V[1], V[1], V[1], 255};
    break;
  case 1: { // LDR luminance, base + offset.
    int L0 = (V[0] >> 2) | (V[1] & 0xC0);
    int L1 = std::min(L0 + (V[1] & 0x3F), 0xFF);
    Lo = {L0, L0, L0, 255};
    Hi = {L1, L1, L1, 255};
    break;
  }
  case 4: // LDR luminance+alpha, direct.
    Lo = {V[0], V[0], V[0], V[2]};
    Hi = {V[1], V[1], V[1], V[3]};
    break;
  case 5: { // LDR luminance+alpha, base + offset.
    bitTransferSigned(V[1], V[0]);
    bitTransferSigned(V[3], V[2]);
    Lo = {V[0], V[0], V[0], V[2]};
    int HiL = V[0] + V[1];
    Hi = {HiL, HiL, HiL, V[2] + V[3]};
    for (int &C : Lo)
      C = clampByte(C);
    for (int &C : Hi)
      C = clampByte(C);
    break;
  }
  case 6: { // LDR RGB, base + scale.
    Hi = {V[0], V[1], V[2], 255};
    for (unsigned I = 0; I != 3; ++I)
      Lo[I] = (Hi[I] * V[3]) >> 8;
    Lo[3] = 255;
    break;
  }
  case 8: { // LDR RGB, direct.
    int S0 = V[0] + V[2] + V[4], S1 = V[1] + V[3] + V[5];
    Lo = {V[0], V[2], V[4], 255};
    Hi = {V[1], V[3], V[5], 255};
    if (S1 < S0) {
      std::swap(Lo, Hi);
      blueContract(Lo);
      blueContract(Hi);
    }
    break;
  }
  case 9: { // LDR RGB, base + offset.
    bitTransferSigned(V[1], V[0]);
    bitTransferSigned(V[3], V[2]);
    bitTransferSigned(V[5], V[4]);
    Lo = {V[0], V[2], V[4], 255};
    Hi = {V[0] + V[1], V[2] + V[3], V[4] + V[5], 255};
    if (V[1] + V[3] + V[5] < 0) {
      std::swap(Lo, Hi);
      blueContract(Lo);
      blueContract(Hi);
    }
    for (int &C : Lo)
      C = clampByte(C);
    for (int &C : Hi)
      C = clampByte(C);
    break;
  }
  case 10: { // LDR RGB, base + scale, plus two alpha values.
    Lo = Hi = {V[0], V[1], V[2], 255};
    for (unsigned I = 0; I != 3; ++I)
      Lo[I] = (Lo[I] * V[3]) >> 8;
    Lo[3] = V[4];
    Hi[3] = V[5];
    break;
  }
  case 12: { // LDR RGBA, direct.
    int S0 = V[0] + V[2] + V[4], S1 = V[1] + V[3] + V[5];
    Lo = {V[0], V[2], V[4], V[6]};
    Hi = {V[1], V[3], V[5], V[7]};
    if (S1 < S0) {
      std::swap(Lo, Hi);
      blueContract(Lo);
      blueContract(Hi);
    }
    break;
  }
  case 13: { // LDR RGBA, base + offset.
    bitTransferSigned(V[1], V[0]);
    bitTransferSigned(V[3], V[2]);
    bitTransferSigned(V[5], V[4]);
    bitTransferSigned(V[7], V[6]);
    Lo = {V[0], V[2], V[4], V[6]};
    Hi = {V[0] + V[1], V[2] + V[3], V[4] + V[5], V[6] + V[7]};
    if (V[1] + V[3] + V[5] < 0) {
      std::swap(Lo, Hi);
      blueContract(Lo);
      blueContract(Hi);
    }
    for (int &C : Lo)
      C = clampByte(C);
    for (int &C : Hi)
      C = clampByte(C);
    break;
  }
  default: // HDR-only mode: out of this decoder's scope (ASTCDecode.h).
    Lo = Hi = {0, 0, 0, 255};
    break;
  }
  return {Lo, Hi};
}

/// A block's own extra CEM bits (specification "more than one partition,
/// no single shared mode") occupy this many bits, immediately following
/// the weight data (before any dual-plane channel selector).
unsigned numExtraCEMBits(unsigned NumPartitions, unsigned SharedCEMField) {
  if (NumPartitions == 1 || SharedCEMField == 0)
    return 0;
  constexpr unsigned ExtraBits[4] = {0, 2, 5, 8};
  return ExtraBits[NumPartitions - 1];
}

} // namespace

void feme::vulkan::decodeASTCBlock(const uint8_t Block[16],
                                   uint32_t BlockWidth, uint32_t BlockHeight,
                                   uint8_t *Output) {
  Block128 V = loadBlock(Block);
  unsigned NumTexels = BlockWidth * BlockHeight;

  auto fillSolid = [&](RGBA Color) {
    for (unsigned I = 0; I != NumTexels; ++I)
      for (unsigned C = 0; C != 4; ++C)
        Output[I * 4 + C] = static_cast<uint8_t>(Color[C]);
  };

  // Void extent: specification "Void Extent Blocks" -- a solid fill for
  // the whole block, with the color stored as four 16-bit values (scaled
  // down to 8 bits here since this decoder is LDR-only, see
  // ASTCDecode.h).
  if (getBits(V, 0, 9) == 0x1FC) {
    if (getBits(V, 9, 1) != 0 || getBits(V, 10, 2) != 0x3) {
      // HDR void extent, or the two reserved bits aren't both set:
      // reserved/out-of-scope encoding.
      fillSolid({0, 0, 0, 255});
      return;
    }
    unsigned R16 = getBits(V, 64, 16);
    unsigned G16 = getBits(V, 80, 16);
    unsigned B16 = getBits(V, 96, 16);
    unsigned A16 = getBits(V, 112, 16);
    fillSolid({int((R16 * 255) / 65535), int((G16 * 255) / 65535),
              int((B16 * 255) / 65535), int((A16 * 255) / 65535)});
    return;
  }

  BlockModeInfo Mode = decodeBlockMode(V);
  if (Mode.Illegal) {
    fillSolid({0, 0, 0, 255});
    return;
  }

  unsigned NumPartitions = 1 + getBits(V, 11, 2);
  // Roadmap E20's own scope note lists "1-or-2 partitions"; this
  // implementation actually covers every partition count the format
  // supports (1-4) at negligible extra cost over 1-2 alone, since
  // `selectPartition` and the CEM-decoding formulas below are already
  // parameterized by partition count either way (see agent_thoughts.md).
  unsigned NumWeights =
      Mode.WeightWidth * Mode.WeightHeight * (Mode.DualPlane ? 2 : 1);
  unsigned WeightBits = iseBitCount(NumWeights, iseRangeShape(Mode.WeightRange));
  unsigned WeightStartBit = 128 - WeightBits;

  unsigned SharedCEMField =
      NumPartitions == 1 ? 0 : getBits(V, 23, 2);
  unsigned ExtraCEMBits = numExtraCEMBits(NumPartitions, SharedCEMField);
  unsigned DualPlaneStartBit =
      WeightStartBit - ExtraCEMBits - (Mode.DualPlane ? 2 : 0);

  // Reserved: four partitions with a dual-plane weight grid together
  // need more selector bits than the format has room for.
  if (NumPartitions == 4 && Mode.DualPlane) {
    fillSolid({0, 0, 0, 255});
    return;
  }

  // Decode each partition's color endpoint mode (specification "Color
  // Endpoint Mode Decoding").
  std::array<unsigned, 4> CEM = {0, 0, 0, 0};
  if (NumPartitions == 1) {
    CEM[0] = getBits(V, 13, 4);
  } else if (ExtraCEMBits == 0) {
    unsigned Shared = getBits(V, 25, 4);
    for (unsigned P = 0; P != NumPartitions; ++P)
      CEM[P] = Shared;
  } else {
    unsigned BaseField = getBits(V, 23, 6);
    unsigned BaseCEM = ((BaseField & 0x3) - 1) * 4;
    unsigned CMBits = BaseField >> 2;
    uint64_t Extra = getBits(V, DualPlaneStartBit + (Mode.DualPlane ? 2 : 0),
                             ExtraCEMBits);
    uint64_t Combined = CMBits | (Extra << 4);
    unsigned C[4] = {0, 0, 0, 0};
    for (unsigned P = 0; P != NumPartitions; ++P) {
      C[P] = Combined & 0x1;
      Combined >>= 1;
    }
    unsigned M[4] = {0, 0, 0, 0};
    for (unsigned P = 0; P != NumPartitions; ++P) {
      M[P] = Combined & 0x3;
      Combined >>= 2;
    }
    for (unsigned P = 0; P != NumPartitions; ++P)
      CEM[P] = BaseCEM + 4 * C[P] + M[P];
  }

  // Total color values across every partition, and the shared ISE range
  // they were all quantized at (specification "Color values share a
  // single range across every partition of the block").
  unsigned NumColorValues = 0;
  for (unsigned P = 0; P != NumPartitions; ++P)
    NumColorValues += numColorValuesForMode(CEM[P]);
  if (NumColorValues > 18) {
    fillSolid({0, 0, 0, 255});
    return;
  }

  unsigned ColorStartBit = NumPartitions == 1 ? 17 : 29;
  unsigned MaxColorBits = DualPlaneStartBit > ColorStartBit
                             ? DualPlaneStartBit - ColorStartBit
                             : 0;
  unsigned ColorRange = 0;
  for (unsigned Range = 255; Range >= 5; --Range) {
    ISERangeShape Shape = iseRangeShape(Range);
    if (!Shape.Valid)
      continue;
    if (iseBitCount(NumColorValues, Shape) <= MaxColorBits) {
      ColorRange = Range;
      break;
    }
  }
  if (ColorRange < 5) {
    fillSolid({0, 0, 0, 255});
    return;
  }

  std::vector<uint32_t> ColorVals =
      decodeISESequence(V, ColorStartBit, NumColorValues, ColorRange);

  std::array<RGBA, 4> EndpointLo{}, EndpointHi{};
  unsigned ColorPos = 0;
  for (unsigned P = 0; P != NumPartitions; ++P) {
    unsigned N = numColorValuesForMode(CEM[P]);
    std::vector<uint32_t> PartVals(ColorVals.begin() + ColorPos,
                                   ColorVals.begin() + ColorPos + N);
    ColorPos += N;
    std::tie(EndpointLo[P], EndpointHi[P]) =
        decodeColorEndpoints(PartVals, ColorRange, CEM[P]);
  }

  // Weight data: read from the block's *high* end, in reverse bit order
  // (specification "Weight Grid" storage direction) -- see
  // `getBitsFromEnd`'s comment.
  unsigned NumGridWeights = Mode.WeightWidth * Mode.WeightHeight;
  std::vector<uint32_t> RawWeights;
  {
    Block128 Reversed;
    // Materialize the reversed weight-region bits into a fresh cursor so
    // `decodeISESequence` (which always reads forward) can be reused
    // as-is for both the plain-plane and dual-plane weight streams.
    for (unsigned I = 0; I != WeightBits; ++I) {
      uint32_t Bit = getBitsFromEnd(V, I, 1);
      if (I < 64)
        Reversed.Lo |= uint64_t(Bit) << I;
      else
        Reversed.Hi |= uint64_t(Bit) << (I - 64);
    }
    RawWeights = decodeISESequence(Reversed, 0, NumWeights, Mode.WeightRange);
  }

  std::vector<unsigned> Grid0(NumGridWeights), Grid1(NumGridWeights);
  unsigned Stride = Mode.DualPlane ? 2 : 1;
  for (unsigned I = 0; I != NumGridWeights; ++I)
    Grid0[I] = unquantizeWeight(RawWeights[I * Stride], Mode.WeightRange);
  if (Mode.DualPlane)
    for (unsigned I = 0; I != NumGridWeights; ++I)
      Grid1[I] = unquantizeWeight(RawWeights[I * Stride + 1], Mode.WeightRange);

  std::vector<unsigned> Weights0 = infillWeights(
      Grid0, Mode.WeightWidth, Mode.WeightHeight, BlockWidth, BlockHeight);
  std::vector<unsigned> Weights1;
  unsigned DualPlaneChannel = 4; // 4 == "no dual plane".
  if (Mode.DualPlane) {
    Weights1 = infillWeights(Grid1, Mode.WeightWidth, Mode.WeightHeight,
                             BlockWidth, BlockHeight);
    DualPlaneChannel = getBits(V, DualPlaneStartBit, 2);
  }

  // Partition assignment, then final per-texel interpolation
  // (specification "Applying the Weights and Endpoints").
  unsigned PartitionID = NumPartitions == 1 ? 0 : getBits(V, 13, 10);
  for (unsigned Y = 0; Y != BlockHeight; ++Y) {
    for (unsigned X = 0; X != BlockWidth; ++X) {
      unsigned Texel = Y * BlockWidth + X;
      unsigned Part =
          selectPartition(PartitionID, X, Y, NumPartitions, NumTexels);
      const RGBA &Lo = EndpointLo[Part];
      const RGBA &Hi = EndpointHi[Part];
      for (unsigned C = 0; C != 4; ++C) {
        unsigned Weight = (Mode.DualPlane && DualPlaneChannel == C)
                             ? Weights1[Texel]
                             : Weights0[Texel];
        unsigned C0 = (unsigned(Lo[C]) << 8) | unsigned(Lo[C]);
        unsigned C1 = (unsigned(Hi[C]) << 8) | unsigned(Hi[C]);
        unsigned Interp = (C0 * (64 - Weight) + C1 * Weight + 32) / 64;
        Output[Texel * 4 + C] =
            static_cast<uint8_t>((Interp * 255 + 32767) / 65536);
      }
    }
  }
}
