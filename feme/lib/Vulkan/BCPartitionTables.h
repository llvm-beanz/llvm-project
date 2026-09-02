//===- BCPartitionTables.h - Shared BC6H/BC7 partition tables -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BC7 and BC6H (both part of the same "BPTC" Khronos bitstream family,
// specified together in one `bptc.txt`) share an identical fixed set of
// 64 2-subset partition patterns and their corresponding second-subset
// anchor-index table -- BC6H uses exactly the same tables BC7Decode.cpp
// already defined for its own 2-subset modes (BC6H has no 3-subset
// modes, so BC7's own `kPartitions3`/`kAnchorSecondSubset3`/
// `kAnchorThirdSubset` stay private to `BC7Decode.cpp`, unneeded here).
// Factored out here (roadmap H8m) so `BC6HDecode.cpp` does not need its
// own duplicate 1024-entry copy of the same data `BC7Decode.cpp` already
// carries.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BCPARTITIONTABLES_H
#define FEME_LIB_VULKAN_BCPARTITIONTABLES_H

#include <cstdint>

namespace feme::vulkan::bcpartitions {

/// The 64 fixed 2-subset partition patterns (one entry per texel, in
/// row-major `4 * y + x` order): which subset (0 or 1) each of a
/// block's 16 texels belongs to, selected by the block's own
/// partition-selection field. Copied verbatim from `VK-GL-CTS`'s own
/// `tcuCompressedTexture.cpp` (`BcDecompressInternal::partitions2`), the
/// actual ground truth this project's own CTS run scores against.
extern const uint8_t Partitions2[64][16];

/// The anchor texel index (in the same row-major order) for subset 1 of
/// a 2-subset block, indexed by the block's own partition-selection
/// value; subset 0's own anchor is always texel 0 and needs no lookup
/// table.
extern const uint8_t AnchorSecondSubset2[64];

} // namespace feme::vulkan::bcpartitions

#endif // FEME_LIB_VULKAN_BCPARTITIONTABLES_H
