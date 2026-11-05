//===- SubresourceLayout.cpp - API-neutral image subresource layout -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See SubresourceLayout.h's file comment: moved verbatim (modulo
// `VkImageType`/`VkDeviceSize` -> `bool Is3D`/`uint64_t`) from
// `feme::vulkan::Image.cpp`'s anonymous-namespace `computeSubresourceLayouts`/
// `computeSubresourceLayout`, WARP roadmap WS0(b).
//
//===----------------------------------------------------------------------===//

#include "feme/Image/SubresourceLayout.h"

#include <algorithm>

using namespace feme::cpu;

namespace feme::image {

std::pair<std::vector<FemeImageSubresourceLayout>, uint64_t>
computeSubresourceLayouts(bool Is3D, uint32_t Width, uint32_t Height,
                          uint32_t Depth, uint32_t MipLevels,
                          uint32_t ArrayLayers, uint32_t SampleCount,
                          uint32_t BlockWidth, uint32_t BlockHeight,
                          uint32_t BytesPerBlock) {
  std::vector<FemeImageSubresourceLayout> Layouts(MipLevels);
  uint64_t Offset = 0;
  for (uint32_t Level = 0; Level != MipLevels; ++Level) {
    uint32_t LevelWidth = std::max(1u, Width >> Level);
    uint32_t LevelHeight = std::max(1u, Height >> Level);
    uint32_t LevelDepth = Is3D ? std::max(1u, Depth >> Level) : 1;
    uint32_t SliceCount = Is3D ? LevelDepth : ArrayLayers;
    uint32_t BlocksWide = (LevelWidth + BlockWidth - 1) / BlockWidth;
    uint32_t BlocksHigh = (LevelHeight + BlockHeight - 1) / BlockHeight;

    FemeImageSubresourceLayout &L = Layouts[Level];
    L.Offset = Offset;
    L.SampleStride = SampleCount > 1 ? BytesPerBlock : 0;
    L.RowPitch = uint64_t(BlocksWide) * BytesPerBlock * SampleCount;
    L.SlicePitch = L.RowPitch * BlocksHigh;
    Offset += L.SlicePitch * SliceCount;
  }
  return {std::move(Layouts), Offset};
}

SubresourceLayout
computeSubresourceLayout(bool Is3D, uint32_t Depth,
                         const FemeImageSubresourceLayout &L,
                         uint32_t MipLevel, uint32_t ArrayLayer) {
  uint32_t LevelDepth = Is3D ? std::max(1u, Depth >> MipLevel) : 1;
  uint64_t Size = Is3D ? L.SlicePitch * LevelDepth : L.SlicePitch;
  return {L.Offset + uint64_t(ArrayLayer) * L.SlicePitch, Size, L.RowPitch,
          /*ArrayPitch=*/Is3D ? 0 : L.SlicePitch,
          /*DepthPitch=*/Is3D ? L.SlicePitch : 0};
}

bool isInBoundsSubresource(bool Is3D, uint32_t Width, uint32_t Height,
                          uint32_t Depth, uint32_t MipLevels,
                          uint32_t ArrayLayers, uint32_t MipLevel,
                          uint32_t ArrayLayer, uint32_t X, uint32_t Y,
                          uint32_t Z, uint32_t UnitWidth,
                          uint32_t UnitHeight) {
  if (MipLevel >= MipLevels)
    return false;
  uint32_t LevelWidth = std::max(1u, Width >> MipLevel);
  uint32_t LevelHeight = std::max(1u, Height >> MipLevel);
  uint32_t LevelDepth = Is3D ? std::max(1u, Depth >> MipLevel) : 1;
  uint32_t SlicesAtLevel = Is3D ? LevelDepth : ArrayLayers;
  uint32_t UnitsWide = (LevelWidth + UnitWidth - 1) / UnitWidth;
  uint32_t UnitsHigh = (LevelHeight + UnitHeight - 1) / UnitHeight;
  return X < UnitsWide && Y < UnitsHigh &&
         uint64_t(ArrayLayer) + Z < SlicesAtLevel;
}

} // namespace feme::image
