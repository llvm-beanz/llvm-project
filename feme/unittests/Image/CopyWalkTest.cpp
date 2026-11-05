//===- CopyWalkTest.cpp - Tests for feme::image::computeImageCopy* -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS1(c): directly exercises the extracted portable
// image-copy region-walk geometry (block/texel unit conversion, row-byte
// sizing, and 3D-depth-slice-vs-array-layer stepping) without any Vulkan
// dependency. `feme/unittests/Vulkan/ImageOpsTest.cpp` (unchanged by this
// extraction, if it exists) continues to cover `feme::vulkan::runCopyImage`
// end to end through the Vulkan object model.
//
//===----------------------------------------------------------------------===//

#include "feme/Image/CopyWalk.h"

#include "gtest/gtest.h"

using namespace feme::image;

namespace {

TEST(CopyWalkTest, UncompressedSameShape2D) {
  // A plain 2D-to-2D copy of a 10x6 region at the origin, single sample,
  // 4-byte texels, no block compression on either side (block shape 1x1).
  ImageCopyRegionWalk Walk = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/1, /*SrcBlockHeight=*/1, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/4, /*SampleCount=*/1,
      /*SrcIs3D=*/false, /*DstIs3D=*/false, /*RegionWidth=*/10,
      /*RegionHeight=*/6, /*RegionDepth=*/1, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/1);
  EXPECT_EQ(Walk.WidthUnits, 10u);
  EXPECT_EQ(Walk.HeightUnits, 6u);
  EXPECT_EQ(Walk.SliceCount, 1u);
  EXPECT_EQ(Walk.RowBytes, 40u);
  EXPECT_EQ(Walk.SrcOffsetXUnits, 0u);
  EXPECT_EQ(Walk.SrcOffsetYUnits, 0u);
  EXPECT_EQ(Walk.DstOffsetXUnits, 0u);
  EXPECT_EQ(Walk.DstOffsetYUnits, 0u);
}

TEST(CopyWalkTest, MultisampleRowBytesFoldsInSampleCount) {
  // Every sample of one texel is stored contiguously, so RowBytes scales
  // linearly with SampleCount for a fixed WidthUnits/UnitSize.
  ImageCopyRegionWalk Walk = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/1, /*SrcBlockHeight=*/1, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/4, /*SampleCount=*/4,
      /*SrcIs3D=*/false, /*DstIs3D=*/false, /*RegionWidth=*/8,
      /*RegionHeight=*/1, /*RegionDepth=*/1, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/1);
  EXPECT_EQ(Walk.WidthUnits, 8u);
  // 8 units * 4 bytes/unit * 4 samples.
  EXPECT_EQ(Walk.RowBytes, 128u);
}

TEST(CopyWalkTest, CompressedSourceBlockUnitConversion) {
  // A BC-family source (4x4 blocks, 16 bytes/block) copied into an
  // uncompressed 16-byte-texel destination (e.g. R32G32B32A32_UINT) --
  // the region extent/offset are always in the *source*'s own block
  // units, so a 32x16 texel region is 8x4 blocks wide/tall on the source
  // side, but the destination offset is independently converted by its
  // own (1x1) block shape.
  ImageCopyRegionWalk Walk = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/4, /*SrcBlockHeight=*/4, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/16, /*SampleCount=*/1,
      /*SrcIs3D=*/false, /*DstIs3D=*/false, /*RegionWidth=*/32,
      /*RegionHeight=*/16, /*RegionDepth=*/1, /*SrcOffsetX=*/8,
      /*SrcOffsetY=*/4, /*DstOffsetX=*/2, /*DstOffsetY=*/3,
      /*ResolvedSrcLayerCount=*/1);
  EXPECT_EQ(Walk.WidthUnits, 8u);
  EXPECT_EQ(Walk.HeightUnits, 4u);
  // SrcOffsetX/Y are in texel units (8, 4); converted to block units (/4).
  EXPECT_EQ(Walk.SrcOffsetXUnits, 2u);
  EXPECT_EQ(Walk.SrcOffsetYUnits, 1u);
  // DstOffsetX/Y are already in the (1x1) destination's own block units.
  EXPECT_EQ(Walk.DstOffsetXUnits, 2u);
  EXPECT_EQ(Walk.DstOffsetYUnits, 3u);
  EXPECT_EQ(Walk.RowBytes, 8u * 16u);
}

TEST(CopyWalkTest, NonDivisibleRegionWidthRoundsUpToFullBlocks) {
  // A region whose extent is not an exact multiple of the source's block
  // shape still walks whole blocks (ceiling division), matching real
  // Vulkan's own rule that a copy region's extent need not align to block
  // boundaries for the last partial block of an image edge.
  ImageCopyRegionWalk Walk = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/4, /*SrcBlockHeight=*/4, /*DstBlockWidth=*/4,
      /*DstBlockHeight=*/4, /*UnitSize=*/16, /*SampleCount=*/1,
      /*SrcIs3D=*/false, /*DstIs3D=*/false, /*RegionWidth=*/10,
      /*RegionHeight=*/5, /*RegionDepth=*/1, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/1);
  // ceil(10/4) = 3, ceil(5/4) = 2.
  EXPECT_EQ(Walk.WidthUnits, 3u);
  EXPECT_EQ(Walk.HeightUnits, 2u);
}

TEST(CopyWalkTest, SliceCountUsesLayerCountWhenNeitherSideIs3D) {
  ImageCopyRegionWalk Walk = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/1, /*SrcBlockHeight=*/1, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/4, /*SampleCount=*/1,
      /*SrcIs3D=*/false, /*DstIs3D=*/false, /*RegionWidth=*/4,
      /*RegionHeight=*/4, /*RegionDepth=*/1, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/5);
  EXPECT_EQ(Walk.SliceCount, 5u);
}

TEST(CopyWalkTest, SliceCountUsesRegionDepthWhenEitherSideIs3D) {
  // Even when only the destination is 3D, extent.depth (not the resolved
  // array-layer count) drives SliceCount.
  ImageCopyRegionWalk WalkDst3D = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/1, /*SrcBlockHeight=*/1, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/4, /*SampleCount=*/1,
      /*SrcIs3D=*/false, /*DstIs3D=*/true, /*RegionWidth=*/4,
      /*RegionHeight=*/4, /*RegionDepth=*/7, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/1);
  EXPECT_EQ(WalkDst3D.SliceCount, 7u);

  ImageCopyRegionWalk WalkSrc3D = computeImageCopyRegionWalk(
      /*SrcBlockWidth=*/1, /*SrcBlockHeight=*/1, /*DstBlockWidth=*/1,
      /*DstBlockHeight=*/1, /*UnitSize=*/4, /*SampleCount=*/1,
      /*SrcIs3D=*/true, /*DstIs3D=*/false, /*RegionWidth=*/4,
      /*RegionHeight=*/4, /*RegionDepth=*/3, /*SrcOffsetX=*/0,
      /*SrcOffsetY=*/0, /*DstOffsetX=*/0, /*DstOffsetY=*/0,
      /*ResolvedSrcLayerCount=*/1);
  EXPECT_EQ(WalkSrc3D.SliceCount, 3u);
}

TEST(CopyWalkTest, SliceLocation2DToSameShapeStepsBothArrayLayers) {
  // Neither side is 3D: both layers step together with the slice index,
  // and neither Z coordinate ever changes from its base offset.
  for (uint32_t S = 0; S != 3; ++S) {
    ImageCopySliceLocation Loc = computeImageCopySliceLocation(
        /*SrcIs3D=*/false, /*DstIs3D=*/false, /*SrcBaseArrayLayer=*/2,
        /*SrcOffsetZ=*/0, /*DstBaseArrayLayer=*/5, /*DstOffsetZ=*/0,
        /*SliceIndex=*/S);
    EXPECT_EQ(Loc.SrcLayer, 2u + S);
    EXPECT_EQ(Loc.SrcZ, 0u);
    EXPECT_EQ(Loc.DstLayer, 5u + S);
    EXPECT_EQ(Loc.DstZ, 0u);
  }
}

TEST(CopyWalkTest, SliceLocation3DSourceStepsZNotLayer) {
  // A 3D source paired with a non-3D destination: only the source's Z
  // coordinate steps; the source's layer and the destination's Z stay
  // fixed, and the destination's layer steps instead (the "2D-array
  // destination addresses 3D depth slices as layers" rule).
  ImageCopySliceLocation Loc = computeImageCopySliceLocation(
      /*SrcIs3D=*/true, /*DstIs3D=*/false, /*SrcBaseArrayLayer=*/0,
      /*SrcOffsetZ=*/4, /*DstBaseArrayLayer=*/1, /*DstOffsetZ=*/0,
      /*SliceIndex=*/3);
  EXPECT_EQ(Loc.SrcLayer, 0u);
  EXPECT_EQ(Loc.SrcZ, 4u + 3u);
  EXPECT_EQ(Loc.DstLayer, 1u + 3u);
  EXPECT_EQ(Loc.DstZ, 0u);
}

TEST(CopyWalkTest, SliceLocationBoth3DStepsBothZNotLayers) {
  ImageCopySliceLocation Loc = computeImageCopySliceLocation(
      /*SrcIs3D=*/true, /*DstIs3D=*/true, /*SrcBaseArrayLayer=*/0,
      /*SrcOffsetZ=*/1, /*DstBaseArrayLayer=*/0, /*DstOffsetZ=*/2,
      /*SliceIndex=*/5);
  EXPECT_EQ(Loc.SrcLayer, 0u);
  EXPECT_EQ(Loc.SrcZ, 1u + 5u);
  EXPECT_EQ(Loc.DstLayer, 0u);
  EXPECT_EQ(Loc.DstZ, 2u + 5u);
}

} // namespace
