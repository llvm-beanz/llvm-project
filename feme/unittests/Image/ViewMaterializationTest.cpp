//===- ViewMaterializationTest.cpp - Tests for feme::image::* ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS1(b): directly exercises the extracted portable
// image-view extent normalization and border-component-mask math without
// any Vulkan dependency. `feme/unittests/Vulkan/CommandBufferTest.cpp`
// (unchanged by this extraction) continues to cover
// `feme::vulkan::materializeImageDescriptor`'s own forwarding wrapper end
// to end through the Vulkan object model.
//
//===----------------------------------------------------------------------===//

#include "feme/Image/ViewMaterialization.h"

#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace feme::image;

namespace {

TEST(ViewMaterializationTest, WholeImage2D) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/64, /*ImageHeight=*/32, /*ImageDepth=*/1,
      /*ImageMipLevels=*/1, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0,
      /*LevelCount=*/1, /*BaseArrayLayer=*/0, /*LayerCount=*/1);
  EXPECT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.Width, 64u);
  EXPECT_EQ(Extent.Height, 32u);
  EXPECT_EQ(Extent.Depth, 1u);
  EXPECT_EQ(Extent.MipLevels, 1u);
  EXPECT_EQ(Extent.ArrayLayers, 1u);
}

TEST(ViewMaterializationTest, RemainingMipLevelsSentinelResolvesToAllLevels) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/16, /*ImageHeight=*/16, /*ImageDepth=*/1,
      /*ImageMipLevels=*/4, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/1,
      /*LevelCount=*/kRemainingImageViewExtent, /*BaseArrayLayer=*/0,
      /*LayerCount=*/1);
  ASSERT_TRUE(Extent.Valid);
  // Starting from mip 1 of 4 (0..3), every remaining level is 1, 2, 3 ->
  // 3 levels.
  EXPECT_EQ(Extent.MipLevels, 3u);
  // Mip 1's own extent is half of the base, per level.
  EXPECT_EQ(Extent.Width, 8u);
  EXPECT_EQ(Extent.Height, 8u);
}

TEST(ViewMaterializationTest, RemainingArrayLayersSentinelResolvesToAllLayers) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/8, /*ImageHeight=*/8, /*ImageDepth=*/1,
      /*ImageMipLevels=*/1, /*ImageArrayLayers=*/6,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0, /*LevelCount=*/1,
      /*BaseArrayLayer=*/2, /*LayerCount=*/kRemainingImageViewExtent);
  ASSERT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.ArrayLayers, 4u); // Layers 2..5 of 6.
}

TEST(ViewMaterializationTest, ExplicitCountsAreClampedNotRejected) {
  // A `levelCount`/`layerCount` that overruns the image's own remaining
  // levels/layers is clamped to what actually exists, the same way the
  // original `materializeImageDescriptor` clamping did, not rejected as
  // invalid.
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/4, /*ImageHeight=*/4, /*ImageDepth=*/1,
      /*ImageMipLevels=*/2, /*ImageArrayLayers=*/2,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0, /*LevelCount=*/100,
      /*BaseArrayLayer=*/0, /*LayerCount=*/100);
  ASSERT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.MipLevels, 2u);
  EXPECT_EQ(Extent.ArrayLayers, 2u);
}

TEST(ViewMaterializationTest, OutOfBoundsBaseMipLevelIsInvalid) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/4, /*ImageHeight=*/4, /*ImageDepth=*/1,
      /*ImageMipLevels=*/2, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/2, /*LevelCount=*/1,
      /*BaseArrayLayer=*/0, /*LayerCount=*/1);
  EXPECT_FALSE(Extent.Valid);
}

TEST(ViewMaterializationTest, OutOfBoundsBaseArrayLayerIsInvalid) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/4, /*ImageHeight=*/4, /*ImageDepth=*/1,
      /*ImageMipLevels=*/1, /*ImageArrayLayers=*/3,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0, /*LevelCount=*/1,
      /*BaseArrayLayer=*/3, /*LayerCount=*/1);
  EXPECT_FALSE(Extent.Valid);
}

TEST(ViewMaterializationTest, ZeroRequestedLayerCountIsInvalid) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/4, /*ImageHeight=*/4, /*ImageDepth=*/1,
      /*ImageMipLevels=*/1, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0, /*LevelCount=*/1,
      /*BaseArrayLayer=*/0, /*LayerCount=*/0);
  EXPECT_FALSE(Extent.Valid);
}

TEST(ViewMaterializationTest, Texture3DDepthHalvesPerMipLevel) {
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/32, /*ImageHeight=*/32, /*ImageDepth=*/16,
      /*ImageMipLevels=*/3, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/true, /*BaseMipLevel=*/2,
      /*LevelCount=*/1, /*BaseArrayLayer=*/0, /*LayerCount=*/1);
  ASSERT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.Width, 8u);
  EXPECT_EQ(Extent.Height, 8u);
  EXPECT_EQ(Extent.Depth, 4u); // 16 >> 2.
}

TEST(ViewMaterializationTest, Non3DViewKeepsDepthDegenerate) {
  // A 2D-array view onto a (hypothetically) deep image still reports
  // Depth == 1: only an actual `Texture3D` view addresses depth slices
  // this way.
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/32, /*ImageHeight=*/32, /*ImageDepth=*/16,
      /*ImageMipLevels=*/1, /*ImageArrayLayers=*/4,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/0, /*LevelCount=*/1,
      /*BaseArrayLayer=*/0, /*LayerCount=*/4);
  ASSERT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.Depth, 1u);
}

TEST(ViewMaterializationTest, MinificationNeverGoesBelowOneTexel) {
  // A 2x2 image's mip 1 is a degenerate 1x1 level, not 0x0.
  ImageViewExtent Extent = resolveImageViewExtent(
      /*ImageWidth=*/2, /*ImageHeight=*/2, /*ImageDepth=*/1,
      /*ImageMipLevels=*/2, /*ImageArrayLayers=*/1,
      /*ViewIsTexture3D=*/false, /*BaseMipLevel=*/1, /*LevelCount=*/1,
      /*BaseArrayLayer=*/0, /*LayerCount=*/1);
  ASSERT_TRUE(Extent.Valid);
  EXPECT_EQ(Extent.Width, 1u);
  EXPECT_EQ(Extent.Height, 1u);
}

TEST(ViewMaterializationTest, BorderComponentMaskThreeChannelFormats) {
  EXPECT_EQ(compressedFormatBorderComponentMask(
                ResourceFormat::BC1_RGB_UNORM),
            0x7u);
  EXPECT_EQ(compressedFormatBorderComponentMask(ResourceFormat::BC6H_UFLOAT),
            0x7u);
  EXPECT_EQ(
      compressedFormatBorderComponentMask(ResourceFormat::ETC2_RGB8_SRGB),
      0x7u);
}

TEST(ViewMaterializationTest, BorderComponentMaskOneChannelFormats) {
  EXPECT_EQ(compressedFormatBorderComponentMask(ResourceFormat::BC4_UNORM),
            0x1u);
  EXPECT_EQ(
      compressedFormatBorderComponentMask(ResourceFormat::EAC_R11_SNORM),
      0x1u);
}

TEST(ViewMaterializationTest, BorderComponentMaskTwoChannelFormats) {
  EXPECT_EQ(compressedFormatBorderComponentMask(ResourceFormat::BC5_SNORM),
            0x3u);
  EXPECT_EQ(
      compressedFormatBorderComponentMask(ResourceFormat::EAC_R11G11_UNORM),
      0x3u);
}

TEST(ViewMaterializationTest, BorderComponentMaskFourChannelFormatsAreZero) {
  // Already-4-channel BC/ETC2 formats, and any uncompressed format, need
  // no override -- 0 tells the caller to derive the mask from `Format`
  // the normal way.
  EXPECT_EQ(compressedFormatBorderComponentMask(ResourceFormat::BC7_UNORM),
            0u);
  EXPECT_EQ(
      compressedFormatBorderComponentMask(ResourceFormat::ETC2_RGBA8_UNORM),
      0u);
  EXPECT_EQ(
      compressedFormatBorderComponentMask(ResourceFormat::R8G8B8A8_UNORM),
      0u);
}

} // namespace
