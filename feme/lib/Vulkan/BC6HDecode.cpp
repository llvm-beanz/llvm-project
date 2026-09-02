//===- BC6HDecode.cpp - BC6H (BPTC HDR) block decoder --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BC6HDecode.h"

#include "BCPartitionTables.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>

using namespace feme::vulkan;

namespace {

/// Loads an 8-byte little-endian half of the 128-bit block.
uint64_t loadLE64(const uint8_t Bytes[8]) {
  uint64_t V = 0;
  for (unsigned I = 0; I != 8; ++I)
    V |= static_cast<uint64_t>(Bytes[I]) << (8 * I);
  return V;
}

/// Reads the inclusive bit range between \p First and \p Last (0..127,
/// at most 32 bits wide) out of the 128-bit block represented as its
/// `Low`/`High` little-endian 64-bit halves, spanning the `Low`/`High`
/// boundary if the range crosses bit 64. Unlike `BC7Decode.cpp`'s own
/// `getBits128` (which never needs it), BC6H's own modes 12 and 13 each
/// store a handful of "extra precision" endpoint bits in a deliberately
/// bit-reversed order relative to every other field -- signaled here by
/// `First > Last` (the caller passes the range's own two endpoints in
/// storage order, which is reversed for those fields), mirroring
/// `VK-GL-CTS`'s own `getBits128`'s identical dual-direction behavior.
uint32_t getBits128(uint64_t Low, uint64_t High, unsigned First,
                     unsigned Last) {
  bool Reverse = First > Last;
  if (Reverse)
    std::swap(First, Last);
  assert(Last - First < 32);
  unsigned FirstWord = First / 64;
  unsigned LastWord = Last / 64;
  uint32_t Ret;
  if (FirstWord == LastWord) {
    uint64_t Word = FirstWord == 0 ? Low : High;
    unsigned Shift = First % 64;
    unsigned Len = Last - First + 1;
    uint64_t Mask = (Len == 64) ? ~uint64_t(0) : ((uint64_t(1) << Len) - 1);
    Ret = static_cast<uint32_t>((Word >> Shift) & Mask);
  } else {
    unsigned Len0 = 64 - First;
    uint32_t Data0 = static_cast<uint32_t>(Low >> First);
    unsigned Len1 = Last - 63;
    uint64_t Mask1 = (uint64_t(1) << Len1) - 1;
    uint32_t Data1 = static_cast<uint32_t>(High & Mask1);
    Ret = Data0 | (Data1 << Len0);
  }
  if (Reverse) {
    unsigned Len = Last - First + 1;
    uint32_t Orig = Ret;
    Ret = 0;
    for (unsigned I = 0; I != Len; ++I)
      Ret |= ((Orig >> (Len - 1 - I)) & 1) << I;
  }
  return Ret;
}

/// BC6H's own 14 modes are selected by the low 2 or 5 bits of the
/// block's own first byte: a low 2-bit value of 0 or 1 selects mode 0
/// or 1 directly (2-subset, delta-encoded); a low 2-bit value of 2 or 3
/// selects one of 8 further modes via the next 3 bits (`2 + n` for
/// 2-subset modes 2-9, `10 + n` for the 4 single-subset direct modes
/// 10-13 plus 4 reserved patterns explicitly rejected below). A first
/// byte whose low 5 bits exactly match one of those 4 reserved patterns
/// is not a valid encoder output for any mode, returned here as -1.
int modeOf(uint8_t FirstByte) {
  switch (FirstByte & 0x1f) {
  case 0x13:
  case 0x17:
  case 0x1b:
  case 0x1f:
    return -1;
  }
  switch (FirstByte & 0x3) {
  case 0:
    return 0;
  case 1:
    return 1;
  case 2:
    return 2 + ((FirstByte >> 2) & 0x7);
  case 3:
    return 10 + ((FirstByte >> 2) & 0x7);
  }
  return -1;
}

/// Total stored bit width of each mode's own first (or only) endpoint
/// pair's R/G/B channels -- also the width every endpoint is finally
/// masked/sign-extended to before unquantization.
const uint8_t kEndpointBits[14] = {10, 7, 11, 11, 11, 9, 8, 8, 8, 6, 10, 11, 12, 16};

/// `round(((64 - w) * A + w * B) / 64)` where `w` is the weight for
/// \p Index at \p IndexBits precision (3 or 4 bits) -- the same
/// weighted-rounding formula `BC7Decode.cpp`'s own `interpolate` uses.
int interpolate(int A, int B, unsigned Index, unsigned IndexBits) {
  static const uint16_t Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
  static const uint16_t Weights4[16] = {0,  4,  9,  13, 17, 21, 26, 30,
                                         34, 38, 43, 47, 51, 55, 60, 64};
  assert(IndexBits == 3 || IndexBits == 4);
  const uint16_t *Weights = IndexBits == 3 ? Weights3 : Weights4;
  // `W` must stay a signed `int` -- BC6H's endpoints may be negative (unlike
  // BC7's), and mixing an unsigned weight with a signed endpoint here would
  // silently promote the endpoint to unsigned and corrupt the result.
  int W = Weights[Index];
  return ((64 - W) * A + W * B + 32) >> 6;
}

/// Sign-extends the low \p SrcBits bits of \p Value (a raw, unsigned
/// bit-field read) to a full 32-bit signed value.
int32_t signExtend(int32_t Value, unsigned SrcBits) {
  uint32_t Sign = Value & (1 << (SrcBits - 1));
  if (!Sign)
    return Value;
  int32_t ExtendedBits = static_cast<int32_t>(~uint32_t(0) << SrcBits);
  return Value | ExtendedBits;
}

/// Maps a sign-extended, mode-width-masked endpoint value into BC6H's
/// own larger intermediate unquantized integer range (still not yet a
/// true half-float numeric value -- `finishUnquantize` below does
/// that), per the specification's own per-mode, signed-vs-unsigned
/// unquantization formula.
int32_t unquantize(int32_t X, int Mode, bool HasSign) {
  unsigned EpBits = kEndpointBits[Mode];
  if (HasSign) {
    bool Negative = false;
    if (EpBits >= 16)
      return X;
    if (X < 0) {
      Negative = true;
      X = -X;
    }
    if (X == 0)
      X = 0;
    else if (X >= ((1 << (EpBits - 1)) - 1))
      X = 0x7fff;
    else
      X = ((X << 15) + 0x4000) >> (EpBits - 1);
    if (Negative)
      X = -X;
    return X;
  }
  if (EpBits >= 15)
    return X;
  if (X == 0)
    return 0;
  if (X == ((1 << EpBits) - 1))
    return 0xffff;
  return ((X << 15) + 0x4000) >> (EpBits - 1);
}

/// The final step converting an interpolated, still-intermediate
/// unquantized value into the true IEEE-754 binary16 bit pattern BC6H
/// ultimately represents: BC6H's own intermediate unquantized range
/// extends slightly beyond a half float's own maximum finite magnitude
/// (`0x7bff`), so this last `* 31 / 32` (signed) or `* 31 / 64`
/// (unsigned) rescale brings it back into half float's own true numeric
/// range before the bit pattern is used as-is.
int16_t finishUnquantize(int32_t X, bool HasSign) {
  if (HasSign) {
    if (X < 0)
      X = -(((-X) * 31) >> 5);
    else
      X = (X * 31) >> 5;
    if (X < 0)
      X = (-X) | 0x8000;
  } else {
    X = (X * 31) / 64;
  }
  return static_cast<int16_t>(X);
}

} // namespace

void feme::vulkan::decodeBC6HBlock(const uint8_t Block[16], bool Signed,
                                    uint16_t *Output) {
  uint64_t Low = loadLE64(Block);
  uint64_t High = loadLE64(Block + 8);
  int Mode = modeOf(Block[0]);

  int R[4] = {};
  int G[4] = {};
  int B[4] = {};
  unsigned DeltaBitsR = 0, DeltaBitsG = 0, DeltaBitsB = 0;
  unsigned PartitionSetId =
      Mode >= 0 && Mode < 10 ? getBits128(Low, High, 77, 81) : 0;
  unsigned NumRegions = Mode >= 10 ? 1 : 2;
  unsigned NumEndpoints = NumRegions * 2;
  bool Transformed = Mode != 9 && Mode != 10;
  unsigned ColorIndexBits = Mode >= 0 && Mode < 10 ? 3 : 4;
  uint64_t ColorIndexData =
      High >> (Mode >= 0 && Mode < 10 ? 18 : 1);
  unsigned AnchorIndex[2] = {
      0, static_cast<unsigned>(
             bcpartitions::AnchorSecondSubset2[PartitionSetId])};

  // Each mode's own bit-packing order is a hand-tuned, mode-specific
  // interleaving of R/G/B/delta fields with a handful of out-of-order
  // "extra precision" bits -- ported directly from `VK-GL-CTS`'s own
  // `decompressBc6H`, the same reasoning `BC7Decode.cpp`'s own
  // partition-table provenance comment gives: this is enumerated,
  // per-mode bit-position data with no formula to derive or verify, and
  // `VK-GL-CTS` is the actual conformance ground truth a real CTS case
  // scores against.
  switch (Mode) {
  case 0:
    G[2] |= getBits128(Low, High, 2, 2) << 4;
    B[2] |= getBits128(Low, High, 3, 3) << 4;
    B[3] |= getBits128(Low, High, 4, 4) << 4;
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 39);
    G[3] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 49);
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 59);
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 75);
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 5;
    break;

  case 1:
    G[2] |= getBits128(Low, High, 2, 2) << 5;
    G[3] |= getBits128(Low, High, 3, 3) << 4;
    G[3] |= getBits128(Low, High, 4, 4) << 5;
    R[0] |= getBits128(Low, High, 5, 11);
    B[3] |= getBits128(Low, High, 12, 12);
    B[3] |= getBits128(Low, High, 13, 13) << 1;
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 21);
    B[2] |= getBits128(Low, High, 22, 22) << 5;
    B[3] |= getBits128(Low, High, 23, 23) << 2;
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 31);
    B[3] |= getBits128(Low, High, 32, 32) << 3;
    B[3] |= getBits128(Low, High, 33, 33) << 5;
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 40);
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 60);
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 70);
    R[3] |= getBits128(Low, High, 71, 76);
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 6;
    break;

  case 2:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 39);
    R[0] |= getBits128(Low, High, 40, 40) << 10;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 48);
    G[0] |= getBits128(Low, High, 49, 49) << 10;
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 58);
    B[0] |= getBits128(Low, High, 59, 59) << 10;
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 75);
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = 5;
    DeltaBitsG = DeltaBitsB = 4;
    break;

  case 3:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 38);
    R[0] |= getBits128(Low, High, 39, 39) << 10;
    G[3] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 49);
    G[0] |= getBits128(Low, High, 50, 50) << 10;
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 58);
    B[0] |= getBits128(Low, High, 59, 59) << 10;
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 68);
    B[3] |= getBits128(Low, High, 69, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 74);
    G[2] |= getBits128(Low, High, 75, 75) << 4;
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsB = 4;
    DeltaBitsG = 5;
    break;

  case 4:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 38);
    R[0] |= getBits128(Low, High, 39, 39) << 10;
    B[2] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 48);
    G[0] |= getBits128(Low, High, 49, 49) << 10;
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 59);
    B[0] |= getBits128(Low, High, 60, 60) << 10;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 68);
    B[3] |= getBits128(Low, High, 69, 69) << 1;
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 74);
    B[3] |= getBits128(Low, High, 75, 75) << 4;
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsG = 4;
    DeltaBitsB = 5;
    break;

  case 5:
    R[0] |= getBits128(Low, High, 5, 13);
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 23);
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 33);
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 39);
    G[3] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 49);
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 59);
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 75);
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 5;
    break;

  case 6:
    R[0] |= getBits128(Low, High, 5, 12);
    G[3] |= getBits128(Low, High, 13, 13) << 4;
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 22);
    B[3] |= getBits128(Low, High, 23, 23) << 2;
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 32);
    B[3] |= getBits128(Low, High, 33, 33) << 3;
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 40);
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 49);
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 59);
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 70);
    R[3] |= getBits128(Low, High, 71, 76);
    DeltaBitsR = 6;
    DeltaBitsG = DeltaBitsB = 5;
    break;

  case 7:
    R[0] |= getBits128(Low, High, 5, 12);
    B[3] |= getBits128(Low, High, 13, 13);
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 22);
    G[2] |= getBits128(Low, High, 23, 23) << 5;
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 32);
    G[3] |= getBits128(Low, High, 33, 33) << 5;
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 39);
    G[3] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 59);
    B[3] |= getBits128(Low, High, 60, 60) << 1;
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 75);
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsB = 5;
    DeltaBitsG = 6;
    break;

  case 8:
    R[0] |= getBits128(Low, High, 5, 12);
    B[3] |= getBits128(Low, High, 13, 13) << 1;
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 22);
    B[2] |= getBits128(Low, High, 23, 23) << 5;
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 32);
    B[3] |= getBits128(Low, High, 33, 33) << 5;
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 39);
    G[3] |= getBits128(Low, High, 40, 40) << 4;
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 49);
    B[3] |= getBits128(Low, High, 50, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 60);
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 69);
    B[3] |= getBits128(Low, High, 70, 70) << 2;
    R[3] |= getBits128(Low, High, 71, 75);
    B[3] |= getBits128(Low, High, 76, 76) << 3;
    DeltaBitsR = DeltaBitsG = 5;
    DeltaBitsB = 6;
    break;

  case 9:
    R[0] |= getBits128(Low, High, 5, 10);
    G[3] |= getBits128(Low, High, 11, 11) << 4;
    B[3] |= getBits128(Low, High, 12, 13);
    B[2] |= getBits128(Low, High, 14, 14) << 4;
    G[0] |= getBits128(Low, High, 15, 20);
    G[2] |= getBits128(Low, High, 21, 21) << 5;
    B[2] |= getBits128(Low, High, 22, 22) << 5;
    B[3] |= getBits128(Low, High, 23, 23) << 2;
    G[2] |= getBits128(Low, High, 24, 24) << 4;
    B[0] |= getBits128(Low, High, 25, 30);
    G[3] |= getBits128(Low, High, 31, 31) << 5;
    B[3] |= getBits128(Low, High, 32, 32) << 3;
    B[3] |= getBits128(Low, High, 33, 33) << 5;
    B[3] |= getBits128(Low, High, 34, 34) << 4;
    R[1] |= getBits128(Low, High, 35, 40);
    G[2] |= getBits128(Low, High, 41, 44);
    G[1] |= getBits128(Low, High, 45, 50);
    G[3] |= getBits128(Low, High, 51, 54);
    B[1] |= getBits128(Low, High, 55, 60);
    B[2] |= getBits128(Low, High, 61, 64);
    R[2] |= getBits128(Low, High, 65, 70);
    R[3] |= getBits128(Low, High, 71, 76);
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 6;
    break;

  case 10:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 44);
    G[1] |= getBits128(Low, High, 45, 54);
    B[1] |= getBits128(Low, High, 55, 64);
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 10;
    break;

  case 11:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 43);
    R[0] |= getBits128(Low, High, 44, 44) << 10;
    G[1] |= getBits128(Low, High, 45, 53);
    G[0] |= getBits128(Low, High, 54, 54) << 10;
    B[1] |= getBits128(Low, High, 55, 63);
    B[0] |= getBits128(Low, High, 64, 64) << 10;
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 9;
    break;

  case 12:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 42);
    R[0] |= getBits128(Low, High, 44, 43) << 10;
    G[1] |= getBits128(Low, High, 45, 52);
    G[0] |= getBits128(Low, High, 54, 53) << 10;
    B[1] |= getBits128(Low, High, 55, 62);
    B[0] |= getBits128(Low, High, 64, 63) << 10;
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 8;
    break;

  case 13:
    R[0] |= getBits128(Low, High, 5, 14);
    G[0] |= getBits128(Low, High, 15, 24);
    B[0] |= getBits128(Low, High, 25, 34);
    R[1] |= getBits128(Low, High, 35, 38);
    R[0] |= getBits128(Low, High, 44, 39) << 10;
    G[1] |= getBits128(Low, High, 45, 48);
    G[0] |= getBits128(Low, High, 54, 49) << 10;
    B[1] |= getBits128(Low, High, 55, 58);
    B[0] |= getBits128(Low, High, 64, 59) << 10;
    DeltaBitsR = DeltaBitsG = DeltaBitsB = 4;
    break;

  default:
    // Mode -1 (reserved/invalid encoding): no fields to extract: the
    // pixel loop below outputs RGB (0, 0, 0) for every texel.
    break;
  }

  if (Mode >= 0) {
    if (Signed) {
      R[0] = signExtend(R[0], kEndpointBits[Mode]);
      G[0] = signExtend(G[0], kEndpointBits[Mode]);
      B[0] = signExtend(B[0], kEndpointBits[Mode]);
    }

    if (Transformed) {
      unsigned Mask = (1u << kEndpointBits[Mode]) - 1;
      for (unsigned I = 1; I != NumEndpoints; ++I) {
        R[I] = signExtend(R[I], DeltaBitsR);
        R[I] = (R[0] + R[I]) & Mask;
        G[I] = signExtend(G[I], DeltaBitsG);
        G[I] = (G[0] + G[I]) & Mask;
        B[I] = signExtend(B[I], DeltaBitsB);
        B[I] = (B[0] + B[I]) & Mask;
      }
    }

    if (Signed) {
      for (unsigned I = 1; I != 4; ++I) {
        R[I] = signExtend(R[I], kEndpointBits[Mode]);
        G[I] = signExtend(G[I], kEndpointBits[Mode]);
        B[I] = signExtend(B[I], kEndpointBits[Mode]);
      }
    }

    for (unsigned I = 0; I != NumEndpoints; ++I) {
      R[I] = unquantize(R[I], Mode, Signed);
      G[I] = unquantize(G[I], Mode, Signed);
      B[I] = unquantize(B[I], Mode, Signed);
    }
  }

  for (unsigned Pixel = 0; Pixel != 16; ++Pixel) {
    unsigned SubsetIndex =
        NumRegions == 1 ? 0
                        : bcpartitions::Partitions2[PartitionSetId][Pixel];
    unsigned Bits = (Pixel == AnchorIndex[SubsetIndex]) ? ColorIndexBits - 1
                                                         : ColorIndexBits;
    unsigned ColorIndex =
        static_cast<unsigned>(ColorIndexData & ((1u << Bits) - 1));

    unsigned Y = Pixel / 4;
    unsigned X = Pixel % 4;
    uint16_t *Texel = Output + (Y * 4 + X) * 3;

    if (Mode < 0) {
      Texel[0] = 0;
      Texel[1] = 0;
      Texel[2] = 0;
    } else {
      int EpStart = 2 * SubsetIndex;
      int EpEnd = 2 * SubsetIndex + 1;
      Texel[0] = static_cast<uint16_t>(finishUnquantize(
          interpolate(R[EpStart], R[EpEnd], ColorIndex, ColorIndexBits),
          Signed));
      Texel[1] = static_cast<uint16_t>(finishUnquantize(
          interpolate(G[EpStart], G[EpEnd], ColorIndex, ColorIndexBits),
          Signed));
      Texel[2] = static_cast<uint16_t>(finishUnquantize(
          interpolate(B[EpStart], B[EpEnd], ColorIndex, ColorIndexBits),
          Signed));
    }

    ColorIndexData >>= Bits;
  }
}
