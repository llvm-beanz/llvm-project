//===- BlockGeometryTest.cpp - Tests for feme::image::* ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS0 ("Separate API-neutral format/block geometry ... from
// Format.*/Image.*"): directly exercises the extracted portable functions
// without any Vulkan dependency, and `feme/unittests/Vulkan/FormatTest.cpp`
// (unchanged by this extraction) continues to cover
// `feme::vulkan::blockWidth`/`blockHeight`/`bytesPerBlock`/
// `formatElementSize`/`isASTCSRGBFormat` as thin forwarders to these same
// functions.
//
//===----------------------------------------------------------------------===//

#include "feme/Image/BlockGeometry.h"

#include "gtest/gtest.h"

using namespace feme::cpu;
using namespace feme::image;

namespace {

TEST(BlockGeometryTest, UnknownFormatHasNoSize) {
  EXPECT_EQ(formatElementSize(ResourceFormat::Unknown), 0u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::Unknown), 0u);
}

TEST(BlockGeometryTest, UnknownFormatHasTrivialBlockShape) {
  // Every non-block-compressed format, including one a caller does not
  // recognize, reports a 1x1 block -- see BlockGeometry.h's "always call
  // this, never branch" comment.
  EXPECT_EQ(blockWidth(ResourceFormat::Unknown), 1u);
  EXPECT_EQ(blockHeight(ResourceFormat::Unknown), 1u);
}

TEST(BlockGeometryTest, OrdinaryFormatElementSizes) {
  EXPECT_EQ(formatElementSize(ResourceFormat::R32_FLOAT), 4u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32_FLOAT), 8u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32_FLOAT), 12u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32A32_FLOAT), 16u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R8_UNORM), 1u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R16_FLOAT), 2u);
}

TEST(BlockGeometryTest, DepthOnly24BitFormatUsesFourByteTexels) {
  EXPECT_EQ(formatElementSize(ResourceFormat::X8_D24_UNORM), 4u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::X8_D24_UNORM), 4u);
  EXPECT_EQ(blockWidth(ResourceFormat::X8_D24_UNORM), 1u);
  EXPECT_EQ(blockHeight(ResourceFormat::X8_D24_UNORM), 1u);
}

TEST(BlockGeometryTest, OrdinaryFormatBlockShapeIsOneByOne) {
  EXPECT_EQ(blockWidth(ResourceFormat::R8G8B8A8_UNORM), 1u);
  EXPECT_EQ(blockHeight(ResourceFormat::R8G8B8A8_UNORM), 1u);
  // For a non-block-compressed format, `bytesPerBlock` and
  // `formatElementSize` agree (one "block" is one texel).
  EXPECT_EQ(bytesPerBlock(ResourceFormat::R8G8B8A8_UNORM),
            formatElementSize(ResourceFormat::R8G8B8A8_UNORM));
}

TEST(BlockGeometryTest, ASTCHasNoSingleTexelSize) {
  // Block-compressed formats report 0 from `formatElementSize` -- see
  // `bytesPerBlock` for their whole-block size instead.
  EXPECT_EQ(formatElementSize(ResourceFormat::ASTC_4x4_UNORM), 0u);
  EXPECT_EQ(formatElementSize(ResourceFormat::ASTC_12x12_SRGB), 0u);
}

TEST(BlockGeometryTest, ASTCBlockShapeMatchesName) {
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_4x4_UNORM), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_4x4_UNORM), 4u);
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_12x10_SRGB), 12u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_12x10_SRGB), 10u);
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_5x4_SFLOAT), 5u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_5x4_SFLOAT), 4u);
}

TEST(BlockGeometryTest, EveryASTCBlockIs128Bits) {
  // Every ASTC footprint, LDR or HDR, packs into the same 16-byte (128-bit)
  // block regardless of its width/height.
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_4x4_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_12x12_SRGB), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_12x12_SFLOAT), 16u);
}

TEST(BlockGeometryTest, BCBlocksAreAlways4x4Texels) {
  EXPECT_EQ(blockWidth(ResourceFormat::BC1_RGB_UNORM), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::BC1_RGB_UNORM), 4u);
  EXPECT_EQ(blockWidth(ResourceFormat::BC7_SRGB), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::BC7_SRGB), 4u);
}

TEST(BlockGeometryTest, SinglePlaneBCFormatsAreHalfTheSizeOfDualPlaneOnes) {
  // BC1/BC4 pack a single 64-bit plane; every other BC format adds a
  // second 64-bit plane, doubling the footprint to 128 bits.
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC1_RGB_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC4_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC2_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC3_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC5_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC6H_UFLOAT), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC7_UNORM), 16u);
}

TEST(BlockGeometryTest, ETC2AndEACBlockSizesMirrorBC) {
  EXPECT_EQ(blockWidth(ResourceFormat::ETC2_RGB8_UNORM), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::ETC2_RGB8_UNORM), 4u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGB8_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGB8A1_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGBA8_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11G11_UNORM), 16u);
}

TEST(BlockGeometryTest, ASTCSRGBDistinguishesFromUNORM) {
  EXPECT_TRUE(isASTCSRGBFormat(ResourceFormat::ASTC_4x4_SRGB));
  EXPECT_FALSE(isASTCSRGBFormat(ResourceFormat::ASTC_4x4_UNORM));
  // Only the LDR family has an sRGB/UNORM distinction; HDR `_SFLOAT`
  // footprints and non-ASTC formats are never sRGB.
  EXPECT_FALSE(isASTCSRGBFormat(ResourceFormat::ASTC_4x4_SFLOAT));
  EXPECT_FALSE(isASTCSRGBFormat(ResourceFormat::R8G8B8A8_UNORM_SRGB));
}

} // namespace
