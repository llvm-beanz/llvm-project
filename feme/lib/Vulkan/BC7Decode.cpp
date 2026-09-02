//===- BC7Decode.cpp - BC7 (BPTC) block decoder -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BC7Decode.h"

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

/// Reads the inclusive bit range `[First, Last]` (0..127, `First <=
/// Last`, at most 32 bits wide) out of the 128-bit block represented as
/// its `Low`/`High` little-endian 64-bit halves, spanning the
/// `Low`/`High` boundary if the range crosses bit 64 -- mirroring
/// `VK-GL-CTS`'s own `getBits128` (the non-reversed case, the only one
/// any BC7 field ever needs).
uint32_t getBits128(uint64_t Low, uint64_t High, unsigned First,
                     unsigned Last) {
  assert(First <= Last && Last - First < 32);
  unsigned FirstWord = First / 64;
  unsigned LastWord = Last / 64;
  if (FirstWord == LastWord) {
    uint64_t Word = FirstWord == 0 ? Low : High;
    unsigned Shift = First % 64;
    unsigned Len = Last - First + 1;
    uint64_t Mask = (Len == 64) ? ~uint64_t(0) : ((uint64_t(1) << Len) - 1);
    return static_cast<uint32_t>((Word >> Shift) & Mask);
  }
  unsigned Len0 = 64 - First;
  uint32_t Data0 = static_cast<uint32_t>(Low >> First);
  unsigned Len1 = Last - 63;
  uint64_t Mask1 = (uint64_t(1) << Len1) - 1;
  uint32_t Data1 = static_cast<uint32_t>(High & Mask1);
  return Data0 | (Data1 << Len0);
}

/// The position of the lowest set bit of the block's own first byte
/// selects which of BC7's 8 modes the rest of the block is laid out as
/// (mode 0 is `xxxxxxx1`, mode 7 is `10000000`); a first byte of
/// exactly zero is not a valid encoder output, returned here as -1.
int modeOf(uint8_t FirstByte) {
  for (int I = 0; I != 8; ++I)
    if (FirstByte & (1 << I))
      return I;
  return -1;
}

// Number of subsets (independent endpoint-color pairs) per mode.
const uint8_t kSubsets[8] = {3, 2, 3, 2, 1, 1, 1, 2};
// Width, in bits, of the explicit partition-selection field per mode
// (0 for modes 4-6, which have exactly one subset and no partition to
// select).
const uint8_t kPartitionBits[8] = {4, 6, 6, 6, 0, 0, 0, 6};
// Per-mode {R, G, B, A, P} raw stored-field bit widths (P is the
// per-endpoint or shared low-order "P-bit"; 0 means the mode has no
// such field, e.g. no alpha or no P-bit at all).
const uint8_t kEndpointBits[8][5] = {
    {4, 4, 4, 0, 1}, {6, 6, 6, 0, 1}, {5, 5, 5, 0, 0}, {7, 7, 7, 0, 1},
    {5, 5, 5, 6, 0}, {7, 7, 7, 8, 0}, {7, 7, 7, 7, 1}, {5, 5, 5, 5, 1}};
// Per-mode primary interpolation-index bit width (before any anchor-bit
// or index-selection-bit adjustment).
const uint8_t kIndexBits[8] = {3, 3, 2, 2, 2, 2, 4, 2};

// The 64 fixed 2-subset partition patterns (one entry per texel, in
// this file's own row-major `4 * y + x` order): which subset (0 or 1)
// each of a block's 16 texels belongs to, selected by the block's own
// partition-selection field. Copied verbatim from `VK-GL-CTS`'s own
// `tcuCompressedTexture.cpp` (`BcDecompressInternal::partitions2`), the
// actual ground truth this project's own CTS run scores against; spot
// checked against the specification's own `bptcP2subset` table.
const uint8_t kPartitions2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0},
    {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1},
    {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0},
    {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0},
    {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1}};

/// The 64 fixed 3-subset partition patterns, same shape and provenance
/// as `kPartitions2` above but assigning each texel to one of 3
/// subsets (0, 1, or 2).
const uint8_t kPartitions3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2},
    {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0},
    {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2},
    {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0},
    {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0},
    {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1},
    {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1},
    {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2},
    {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2},
    {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1},
    {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0}};

// The anchor texel index (in this file's own row-major `4 * y + x`
// order) for subset 1 of a 2-subset block, indexed by the block's own
// partition-selection value; subset 0's own anchor is always texel 0
// and needs no lookup table.
const uint8_t kAnchorSecondSubset2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 2,  8,  2,  2,  8,  8,  15, 2,  8,  2,  2,  8,  8,  2,  2,
    15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6,
    6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2,  15};

// The anchor texel index for subset 1 of a 3-subset block.
const uint8_t kAnchorSecondSubset3[64] = {
    3, 3,  15, 15, 8, 3,  15, 15, 8,  8, 6,  6, 6,  5,  3,  3,
    3, 3,  8,  15, 3, 3,  6,  10, 5,  8, 8,  6, 8,  5,  15, 15,
    8, 15, 3,  5,  6, 10, 8,  15, 15, 3, 15, 5, 15, 15, 15, 15,
    3, 15, 5,  5,  5, 8,  5,  10, 5,  10, 8, 13, 15, 12, 3,  3};

// The anchor texel index for subset 2 of a 3-subset block.
const uint8_t kAnchorThirdSubset[64] = {
    15, 8, 8,  3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,
    15, 8, 15, 3,  15, 8,  15, 8,  3,  15, 6,  10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8,  9,  10, 6,  15, 8,  15, 3,  6,  6,  8,
    15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8};

// Interpolation weights (of 64) for a 2/3/4-bit index value, indexed by
// the raw index -- BC7's own fixed rounding-interpolation table, shared
// by every mode and every channel (color and alpha alike), unlike
// BC1-5's own per-mode-distinct truncating formulas.
const uint16_t kWeights2[4] = {0, 21, 43, 64};
const uint16_t kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
const uint16_t kWeights4[16] = {0,  4,  9,  13, 17, 21, 26, 30,
                                34, 38, 43, 47, 51, 55, 60, 64};

/// `round(((64 - w) * A + w * B) / 64)` where `w` is the weight for
/// `Index` at `IndexBits` precision (2, 3, or 4 bits) -- BC7's own
/// shared endpoint-interpolation formula.
int interpolate(int A, int B, unsigned Index, unsigned IndexBits) {
  assert(IndexBits >= 2 && IndexBits <= 4);
  const uint16_t *Weights =
      IndexBits == 2 ? kWeights2 : (IndexBits == 3 ? kWeights3 : kWeights4);
  unsigned W = Weights[Index];
  return ((64 - W) * A + W * B + 32) >> 6;
}

/// One decoded endpoint's 4 channels (R, G, B, A), already normalized
/// to a full 8-bit range.
struct Endpoint {
  int Channel[4];
};

} // namespace

void feme::vulkan::decodeBC7Block(const uint8_t Block[16], uint8_t *Output) {
  uint64_t Low = loadLE64(Block);
  uint64_t High = loadLE64(Block + 8);
  int Mode = modeOf(Block[0]);

  if (Mode < 0) {
    // Not a valid encoder output per the specification, but a decoder
    // must still not misbehave on it -- every texel decodes to fully
    // transparent black, mirroring `VK-GL-CTS`'s own reference decoder.
    std::memset(Output, 0, 64);
    return;
  }

  unsigned NumSubsets = kSubsets[Mode];
  unsigned Offset = static_cast<unsigned>(Mode) + 1;
  unsigned PartitionSetId = 0;
  unsigned Rotation = 0;
  unsigned IdxMode = 0;

  // Modes 0-3 and 7 have an explicit partition-selection field; modes
  // 4-6 have exactly one subset and no partition to select.
  if (Mode == 0 || Mode == 1 || Mode == 2 || Mode == 3 || Mode == 7) {
    PartitionSetId =
        getBits128(Low, High, Offset, Offset + kPartitionBits[Mode] - 1);
    Offset += kPartitionBits[Mode];
  }

  // Only modes 4 and 5 support per-block channel rotation; mode 4 also
  // has its own index-selection bit (which of its two index fields
  // drives color vs. alpha).
  if (Mode == 4 || Mode == 5) {
    Rotation = getBits128(Low, High, Offset, Offset + 1);
    Offset += 2;
    if (Mode == 4) {
      IdxMode = getBits128(Low, High, Offset, Offset);
      ++Offset;
    }
  }

  unsigned NumEndpoints = NumSubsets * 2;
  // Raw per-endpoint, per-component (R, G, B, A, P) stored bits, before
  // P-bit folding and MSB-replication extension to 8 bits.
  unsigned Raw[6][5] = {};
  for (unsigned Cpnt = 0; Cpnt != 5; ++Cpnt) {
    for (unsigned Ep = 0; Ep != NumEndpoints; ++Ep) {
      // Mode 1 has exactly one shared P-bit pair (not one per
      // endpoint): only endpoints 0 and 1 store a P-bit field at all.
      if (Mode == 1 && Cpnt == 4 && Ep > 1)
        continue;
      unsigned N = kEndpointBits[Mode][Cpnt];
      if (N > 0)
        Raw[Ep][Cpnt] = getBits128(Low, High, Offset, Offset + N - 1);
      Offset += N;
    }
  }

  Endpoint Endpoints[6];
  // Modes with any P-bit (per-endpoint or shared) fold that bit in as
  // the new low bit of each color/alpha channel before extension.
  bool HasPBit = kEndpointBits[Mode][4] > 0;
  if (HasPBit) {
    for (unsigned Ep = 0; Ep != NumEndpoints; ++Ep)
      for (unsigned Cpnt = 0; Cpnt != 4; ++Cpnt)
        Raw[Ep][Cpnt] <<= 1;
    if (Mode == 1) {
      // Shared P-bit: endpoints 0/1 (subset 0) use P-bit 0's own value,
      // endpoints 2/3 (subset 1) use P-bit 1's.
      unsigned PBit0 = Raw[0][4];
      unsigned PBit1 = Raw[1][4];
      for (unsigned Cpnt = 0; Cpnt != 3; ++Cpnt) {
        Raw[0][Cpnt] |= PBit0;
        Raw[1][Cpnt] |= PBit0;
        Raw[2][Cpnt] |= PBit1;
        Raw[3][Cpnt] |= PBit1;
      }
    } else {
      for (unsigned Ep = 0; Ep != NumEndpoints; ++Ep)
        for (unsigned Cpnt = 0; Cpnt != 4; ++Cpnt)
          Raw[Ep][Cpnt] |= Raw[Ep][4];
    }
  }

  for (unsigned Ep = 0; Ep != NumEndpoints; ++Ep) {
    for (unsigned Cpnt = 0; Cpnt != 4; ++Cpnt) {
      // Left-shift so the stored value's own MSB lands in bit 7, then
      // replicate that MSB into the newly-opened low bits -- the same
      // bit-replication extension convention BC1-5's own RGB565
      // unpacking uses, generalized to a mode-dependent source width.
      unsigned UsedBits = kEndpointBits[Mode][Cpnt] + kEndpointBits[Mode][4];
      unsigned Shift = 8 - UsedBits;
      unsigned V = Raw[Ep][Cpnt] << Shift;
      Endpoints[Ep].Channel[Cpnt] = static_cast<int>(V | (V >> UsedBits));
    }
    // Modes 0-3 store no alpha field at all; alpha is always opaque.
    if (Mode < 4)
      Endpoints[Ep].Channel[3] = 255;
  }

  unsigned ColorIdxOffset = Offset + ((Mode == 4 && IdxMode) ? 31 : 0);
  unsigned AlphaIdxOffset =
      Offset + ((Mode == 5 || (Mode == 4 && !IdxMode)) ? 31 : 0);

  for (unsigned Pixel = 0; Pixel != 16; ++Pixel) {
    unsigned Y = Pixel / 4;
    unsigned X = Pixel % 4;
    uint8_t *Texel = Output + (Y * 4 + X) * 4;

    unsigned SubsetIndex = 0;
    if (NumSubsets == 2)
      SubsetIndex = kPartitions2[PartitionSetId][Pixel];
    else if (NumSubsets == 3)
      SubsetIndex = kPartitions3[PartitionSetId][Pixel];

    unsigned AnchorIndex = 0;
    if (NumSubsets == 2 && SubsetIndex == 1)
      AnchorIndex = kAnchorSecondSubset2[PartitionSetId];
    else if (NumSubsets == 3 && SubsetIndex == 1)
      AnchorIndex = kAnchorSecondSubset3[PartitionSetId];
    else if (NumSubsets == 3 && SubsetIndex == 2)
      AnchorIndex = kAnchorThirdSubset[PartitionSetId];

    const Endpoint &Start = Endpoints[2 * SubsetIndex];
    const Endpoint &End = Endpoints[2 * SubsetIndex + 1];

    unsigned ColorInterpolationBits = kIndexBits[Mode] + IdxMode;
    unsigned ColorIndexBits =
        ColorInterpolationBits - (AnchorIndex == Pixel ? 1 : 0);
    unsigned AlphaInterpolationBits =
        Mode == 4 ? (3 - IdxMode)
                  : (Mode == 5 ? 2 : ColorInterpolationBits);
    unsigned AlphaIndexBits =
        AlphaInterpolationBits - (AnchorIndex == Pixel ? 1 : 0);

    unsigned ColorIdx =
        getBits128(Low, High, ColorIdxOffset, ColorIdxOffset + ColorIndexBits - 1);
    unsigned AlphaIdx =
        (Mode == 4 || Mode == 5)
            ? getBits128(Low, High, AlphaIdxOffset,
                         AlphaIdxOffset + AlphaIndexBits - 1)
            : ColorIdx;
    ColorIdxOffset += ColorIndexBits;
    AlphaIdxOffset += AlphaIndexBits;

    int R = interpolate(Start.Channel[0], End.Channel[0], ColorIdx,
                         ColorInterpolationBits);
    int G = interpolate(Start.Channel[1], End.Channel[1], ColorIdx,
                         ColorInterpolationBits);
    int B = interpolate(Start.Channel[2], End.Channel[2], ColorIdx,
                         ColorInterpolationBits);
    int A = interpolate(Start.Channel[3], End.Channel[3], AlphaIdx,
                         AlphaInterpolationBits);

    // Modes 4/5's own rotation bits swap a color channel with alpha in
    // the final output only -- interpolation above always happens on
    // the block's own native R/G/B/A channel assignment.
    if ((Mode == 4 || Mode == 5) && Rotation != 0) {
      switch (Rotation) {
      case 1:
        std::swap(A, R);
        break;
      case 2:
        std::swap(A, G);
        break;
      case 3:
        std::swap(A, B);
        break;
      default:
        break;
      }
    }

    Texel[0] = static_cast<uint8_t>(R);
    Texel[1] = static_cast<uint8_t>(G);
    Texel[2] = static_cast<uint8_t>(B);
    Texel[3] = static_cast<uint8_t>(A);
  }
}
