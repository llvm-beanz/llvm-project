//===- CopyWalk.cpp - API-neutral image-copy region walking -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Image/CopyWalk.h"

using namespace feme::image;

ImageCopyRegionWalk feme::image::computeImageCopyRegionWalk(
    uint32_t SrcBlockWidth, uint32_t SrcBlockHeight, uint32_t DstBlockWidth,
    uint32_t DstBlockHeight, uint32_t UnitSize, uint32_t SampleCount,
    bool SrcIs3D, bool DstIs3D, uint32_t RegionWidth, uint32_t RegionHeight,
    uint32_t RegionDepth, uint32_t SrcOffsetX, uint32_t SrcOffsetY,
    uint32_t DstOffsetX, uint32_t DstOffsetY, uint32_t ResolvedSrcLayerCount) {
  ImageCopyRegionWalk Walk;
  Walk.WidthUnits = (RegionWidth + SrcBlockWidth - 1) / SrcBlockWidth;
  Walk.HeightUnits = (RegionHeight + SrcBlockHeight - 1) / SrcBlockHeight;
  Walk.SrcOffsetXUnits = SrcOffsetX / SrcBlockWidth;
  Walk.SrcOffsetYUnits = SrcOffsetY / SrcBlockHeight;
  Walk.DstOffsetXUnits = DstOffsetX / DstBlockWidth;
  Walk.DstOffsetYUnits = DstOffsetY / DstBlockHeight;
  Walk.RowBytes = uint64_t(Walk.WidthUnits) * UnitSize * SampleCount;
  Walk.SliceCount =
      (SrcIs3D || DstIs3D) ? RegionDepth : ResolvedSrcLayerCount;
  return Walk;
}

ImageCopySliceLocation feme::image::computeImageCopySliceLocation(
    bool SrcIs3D, bool DstIs3D, uint32_t SrcBaseArrayLayer,
    uint32_t SrcOffsetZ, uint32_t DstBaseArrayLayer, uint32_t DstOffsetZ,
    uint32_t SliceIndex) {
  ImageCopySliceLocation Location;
  Location.SrcLayer = SrcBaseArrayLayer + (SrcIs3D ? 0 : SliceIndex);
  Location.SrcZ = SrcOffsetZ + (SrcIs3D ? SliceIndex : 0);
  Location.DstLayer = DstBaseArrayLayer + (DstIs3D ? 0 : SliceIndex);
  Location.DstZ = DstOffsetZ + (DstIs3D ? SliceIndex : 0);
  return Location;
}
