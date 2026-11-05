//===- SubresourceLayoutTest.cpp - Tests for feme::image::* --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS0(b): directly exercises the extracted portable
// subresource-layout math without any Vulkan dependency.
// `feme/unittests/Vulkan/ImageTest.cpp` (unchanged by this extraction)
// continues to cover `feme::vulkan::Image`'s own forwarding wrappers
// (`computeImageCreateInfoSize`, `computeImageCreateInfoSubresourceLayout`,
// `Image::subresourceLayout`) end to end through the Vulkan object model.
//
//===----------------------------------------------------------------------===//

#include "feme/Image/SubresourceLayout.h"

#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace feme::image;

namespace {

TEST(SubresourceLayoutTest, SingleMip2DPlainTexel) {
  // 4x4 non-block-compressed, 1 byte/texel, single array layer: one
  // 4-row-by-4-byte level, no padding.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*SampleCount=*/1,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/1);
  ASSERT_EQ(Layouts.size(), 1u);
  EXPECT_EQ(Layouts[0].Offset, 0u);
  EXPECT_EQ(Layouts[0].RowPitch, 4u);
  EXPECT_EQ(Layouts[0].SlicePitch, 16u);
  EXPECT_EQ(Layouts[0].SampleStride, 0u);
  EXPECT_EQ(TotalSize, 16u);
}

TEST(SubresourceLayoutTest, MultiMip2DHalvesPerLevel) {
  // 8x8, 3 mips (8x8, 4x4, 2x2): each level's offset is the sum of every
  // prior level's SlicePitch (single array layer, so SliceCount == 1).
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/8, /*Height=*/8, /*Depth=*/1,
      /*MipLevels=*/3, /*ArrayLayers=*/1, /*SampleCount=*/1,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/1);
  ASSERT_EQ(Layouts.size(), 3u);
  EXPECT_EQ(Layouts[0].Offset, 0u);
  EXPECT_EQ(Layouts[0].SlicePitch, 64u);
  EXPECT_EQ(Layouts[1].Offset, 64u);
  EXPECT_EQ(Layouts[1].SlicePitch, 16u);
  EXPECT_EQ(Layouts[2].Offset, 80u);
  EXPECT_EQ(Layouts[2].SlicePitch, 4u);
  EXPECT_EQ(TotalSize, 84u);
}

TEST(SubresourceLayoutTest, ArrayLayersMultiplySlicePitchIntoOffset) {
  // A non-3D image's slice count is the constant ArrayLayers at every mip
  // (unlike a 3D image's halving depth), so Offset/TotalSize scale
  // linearly with ArrayLayers.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/5, /*SampleCount=*/1,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/1);
  ASSERT_EQ(Layouts.size(), 1u);
  EXPECT_EQ(Layouts[0].SlicePitch, 16u);
  EXPECT_EQ(TotalSize, 5u * 16u);
}

TEST(SubresourceLayoutTest, Depth3DHalvesPerLevelInsteadOfArrayLayers) {
  // A 3D image's "slice count" is its own halving depth, not ArrayLayers
  // (which Vulkan requires to be 1 for a 3D image); mip 0 has depth 4, mip
  // 1 has depth 2.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/true, /*Width=*/4, /*Height=*/4, /*Depth=*/4,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*SampleCount=*/1,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/1);
  ASSERT_EQ(Layouts.size(), 2u);
  EXPECT_EQ(Layouts[0].SlicePitch, 16u);
  EXPECT_EQ(Layouts[0].Offset, 0u);
  // Level 0 occupies SlicePitch * depth-at-level-0 (4) == 64 bytes.
  EXPECT_EQ(Layouts[1].Offset, 64u);
  EXPECT_EQ(Layouts[1].SlicePitch, 4u);
  // Level 1's depth is max(1, 4 >> 1) == 2, so it occupies 4 * 2 == 8
  // bytes.
  EXPECT_EQ(TotalSize, 64u + 8u);
}

TEST(SubresourceLayoutTest, BlockCompressedRoundsUpToWholeBlocks) {
  // A 5x5 extent with 4x4 blocks needs ceil(5/4) == 2 blocks per
  // dimension, i.e. a 2x2 block grid, even though only one block's worth
  // of texels (4x4) would otherwise fit without the +BlockWidth-1 bias.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/5, /*Height=*/5, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*SampleCount=*/1,
      /*BlockWidth=*/4, /*BlockHeight=*/4, /*BytesPerBlock=*/16);
  ASSERT_EQ(Layouts.size(), 1u);
  EXPECT_EQ(Layouts[0].RowPitch, 2u * 16u);
  EXPECT_EQ(Layouts[0].SlicePitch, 2u * 16u * 2u);
  EXPECT_EQ(TotalSize, 2u * 16u * 2u);
}

TEST(SubresourceLayoutTest, MultisampleSetsSampleStrideAndScalesRowPitch) {
  // A multisample, non-block-compressed image stores every sample of a
  // texel contiguously: SampleStride == BytesPerBlock, and a row is
  // SampleCount times as wide in bytes as the single-sample case.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*SampleCount=*/4,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/2);
  ASSERT_EQ(Layouts.size(), 1u);
  EXPECT_EQ(Layouts[0].SampleStride, 2u);
  EXPECT_EQ(Layouts[0].RowPitch, 4u * 2u * 4u);
  EXPECT_EQ(TotalSize, Layouts[0].RowPitch * 4u);
}

TEST(SubresourceLayoutTest, SingleSampleLeavesSampleStrideZero) {
  // SampleStride is only set for SampleCount > 1 -- see the function's own
  // comment; a single-sample image reports 0, matching
  // `feme::cpu::FemeImageSubresourceLayout`'s documented "0 for a
  // single-sample image" convention.
  auto [Layouts, TotalSize] = computeSubresourceLayouts(
      /*Is3D=*/false, /*Width=*/2, /*Height=*/2, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*SampleCount=*/1,
      /*BlockWidth=*/1, /*BlockHeight=*/1, /*BytesPerBlock=*/4);
  ASSERT_EQ(Layouts.size(), 1u);
  EXPECT_EQ(Layouts[0].SampleStride, 0u);
  (void)TotalSize;
}

TEST(SubresourceLayoutTest, SubresourceLayoutNonArrayNon3D) {
  // A 2D (non-array, non-3D) image's ArrayPitch equals its SlicePitch and
  // DepthPitch is 0 -- see SubresourceLayout.h's own comment on the
  // Size/ArrayPitch/DepthPitch rules.
  FemeImageSubresourceLayout L{/*Offset=*/100, /*RowPitch=*/4,
                               /*SlicePitch=*/16, /*SampleStride=*/0};
  SubresourceLayout R =
      computeSubresourceLayout(/*Is3D=*/false, /*Depth=*/1, L,
                               /*MipLevel=*/0, /*ArrayLayer=*/0);
  EXPECT_EQ(R.Offset, 100u);
  EXPECT_EQ(R.Size, 16u);
  EXPECT_EQ(R.RowPitch, 4u);
  EXPECT_EQ(R.ArrayPitch, 16u);
  EXPECT_EQ(R.DepthPitch, 0u);
}

TEST(SubresourceLayoutTest, SubresourceLayoutArrayLayerOffsetsByLayer) {
  // An array image's ArrayLayer N starts N * SlicePitch bytes past the
  // mip level's own base offset.
  FemeImageSubresourceLayout L{/*Offset=*/0, /*RowPitch=*/4,
                               /*SlicePitch=*/16, /*SampleStride=*/0};
  SubresourceLayout R =
      computeSubresourceLayout(/*Is3D=*/false, /*Depth=*/1, L,
                               /*MipLevel=*/0, /*ArrayLayer=*/3);
  EXPECT_EQ(R.Offset, 3u * 16u);
  EXPECT_EQ(R.Size, 16u);
}

TEST(SubresourceLayoutTest, InBoundsRejectsMipLevelPastCount) {
  EXPECT_FALSE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*MipLevel=*/2, /*ArrayLayer=*/0,
      /*X=*/0, /*Y=*/0, /*Z=*/0, /*UnitWidth=*/1, /*UnitHeight=*/1));
}

TEST(SubresourceLayoutTest, InBoundsAcceptsCoordinateInsideHalvedMipExtent) {
  // Mip level 1 of an 8x8 image is 4x4 texels: (3, 3) is in bounds, (4, 0)
  // is not.
  EXPECT_TRUE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/8, /*Height=*/8, /*Depth=*/1,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*MipLevel=*/1, /*ArrayLayer=*/0,
      /*X=*/3, /*Y=*/3, /*Z=*/0, /*UnitWidth=*/1, /*UnitHeight=*/1));
  EXPECT_FALSE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/8, /*Height=*/8, /*Depth=*/1,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*MipLevel=*/1, /*ArrayLayer=*/0,
      /*X=*/4, /*Y=*/0, /*Z=*/0, /*UnitWidth=*/1, /*UnitHeight=*/1));
}

TEST(SubresourceLayoutTest, InBoundsAcceptsBlockGridUnits) {
  // A 5x5 extent with 4x4 blocks has a 2x2 block grid: (1, 1) in bounds,
  // (2, 0) is not.
  EXPECT_TRUE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/5, /*Height=*/5, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*MipLevel=*/0, /*ArrayLayer=*/0,
      /*X=*/1, /*Y=*/1, /*Z=*/0, /*UnitWidth=*/4, /*UnitHeight=*/4));
  EXPECT_FALSE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/5, /*Height=*/5, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/1, /*MipLevel=*/0, /*ArrayLayer=*/0,
      /*X=*/2, /*Y=*/0, /*Z=*/0, /*UnitWidth=*/4, /*UnitHeight=*/4));
}

TEST(SubresourceLayoutTest, InBoundsChecksArrayLayerAgainstArrayLayers) {
  EXPECT_TRUE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/3, /*MipLevel=*/0, /*ArrayLayer=*/2,
      /*X=*/0, /*Y=*/0, /*Z=*/0, /*UnitWidth=*/1, /*UnitHeight=*/1));
  EXPECT_FALSE(isInBoundsSubresource(
      /*Is3D=*/false, /*Width=*/4, /*Height=*/4, /*Depth=*/1,
      /*MipLevels=*/1, /*ArrayLayers=*/3, /*MipLevel=*/0, /*ArrayLayer=*/3,
      /*X=*/0, /*Y=*/0, /*Z=*/0, /*UnitWidth=*/1, /*UnitHeight=*/1));
}

TEST(SubresourceLayoutTest, InBoundsChecksZAgainstHalvedDepthFor3D) {
  // An 8-deep 3D image's mip level 1 has depth max(1, 8 >> 1) == 4:
  // Z == 3 in bounds, Z == 4 is not.
  EXPECT_TRUE(isInBoundsSubresource(
      /*Is3D=*/true, /*Width=*/4, /*Height=*/4, /*Depth=*/8,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*MipLevel=*/1, /*ArrayLayer=*/0,
      /*X=*/0, /*Y=*/0, /*Z=*/3, /*UnitWidth=*/1, /*UnitHeight=*/1));
  EXPECT_FALSE(isInBoundsSubresource(
      /*Is3D=*/true, /*Width=*/4, /*Height=*/4, /*Depth=*/8,
      /*MipLevels=*/2, /*ArrayLayers=*/1, /*MipLevel=*/1, /*ArrayLayer=*/0,
      /*X=*/0, /*Y=*/0, /*Z=*/4, /*UnitWidth=*/1, /*UnitHeight=*/1));
}

TEST(SubresourceLayoutTest, SubresourceLayout3DCoversWholeDepthRange) {
  // A 3D image's Size is its *whole* depth range (SlicePitch * depth at
  // this mip level), not one array layer's own range (a 3D image has
  // exactly one array layer covering every depth slice); ArrayPitch is 0
  // and DepthPitch is SlicePitch, the mirror image of the non-3D case.
  FemeImageSubresourceLayout L{/*Offset=*/0, /*RowPitch=*/4,
                               /*SlicePitch=*/16, /*SampleStride=*/0};
  SubresourceLayout R = computeSubresourceLayout(
      /*Is3D=*/true, /*Depth=*/8, L, /*MipLevel=*/1, /*ArrayLayer=*/0);
  // Depth at mip level 1 is max(1, 8 >> 1) == 4.
  EXPECT_EQ(R.Size, 16u * 4u);
  EXPECT_EQ(R.ArrayPitch, 0u);
  EXPECT_EQ(R.DepthPitch, 16u);
}

} // namespace
