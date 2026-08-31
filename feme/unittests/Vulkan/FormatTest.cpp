//===- FormatTest.cpp - VkFormat -> ResourceFormat mapping tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Format.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;
using feme::cpu::ResourceFormat;

namespace {

TEST(FormatTest, MapsIdentityFloatFormats) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R32_SFLOAT), ResourceFormat::R32_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R32G32B32A32_SFLOAT),
            ResourceFormat::R32G32B32A32_FLOAT);
}

TEST(FormatTest, MapsPackedFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8B8A8_UNORM),
            ResourceFormat::R8G8B8A8_UNORM);
}

TEST(FormatTest, MapsDepthStencilFormats) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_D32_SFLOAT), ResourceFormat::D32_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_D24_UNORM_S8_UINT),
            ResourceFormat::D24_UNORM_S8_UINT);
}

TEST(FormatTest, MapsMaintenance5Formats) {
  // Roadmap E5: `VK_KHR_maintenance5`'s two new formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8_UNORM_KHR), ResourceFormat::A8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR),
            ResourceFormat::A1B5G5R5_UNORM);
}

TEST(FormatTest, Maps4444Formats) {
  // Roadmap E19: `VK_EXT_4444_formats`'s two new packed formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT),
            ResourceFormat::A4R4G4B4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT),
            ResourceFormat::A4B4G4R4_UNORM);
}

TEST(FormatTest, MapsRemainingPackedSixteenBitFormats) {
  // Roadmap H7r: the remaining core Vulkan 1.0 packed 16-bit formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R4G4B4A4_UNORM_PACK16),
            ResourceFormat::R4G4B4A4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_B4G4R4A4_UNORM_PACK16),
            ResourceFormat::B4G4R4A4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R5G6B5_UNORM_PACK16),
            ResourceFormat::R5G6B5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_B5G6R5_UNORM_PACK16),
            ResourceFormat::B5G6R5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R5G5B5A1_UNORM_PACK16),
            ResourceFormat::R5G5B5A1_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_B5G5R5A1_UNORM_PACK16),
            ResourceFormat::B5G5R5A1_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A1R5G5B5_UNORM_PACK16),
            ResourceFormat::A1R5G5B5_UNORM);
  for (ResourceFormat Format :
       {ResourceFormat::R4G4B4A4_UNORM, ResourceFormat::B4G4R4A4_UNORM,
        ResourceFormat::R5G6B5_UNORM, ResourceFormat::B5G6R5_UNORM,
        ResourceFormat::R5G5B5A1_UNORM, ResourceFormat::B5G5R5A1_UNORM,
        ResourceFormat::A1R5G5B5_UNORM})
    EXPECT_EQ(formatElementSize(Format), 2u);
}

TEST(FormatTest, MapsSingleChannelR8Formats) {
  // Roadmap H19j: the single-channel `R8` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8_UNORM), ResourceFormat::R8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8_SNORM), ResourceFormat::R8_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8_UINT), ResourceFormat::R8_UINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8_SINT), ResourceFormat::R8_SINT);
  for (ResourceFormat Format :
       {ResourceFormat::R8_UNORM, ResourceFormat::R8_SNORM,
        ResourceFormat::R8_UINT, ResourceFormat::R8_SINT})
    EXPECT_EQ(formatElementSize(Format), 1u);
}

TEST(FormatTest, MapsTwoChannelR8G8Formats) {
  // Roadmap H19n: the two-channel `R8G8` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8_UNORM), ResourceFormat::R8G8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8_SNORM), ResourceFormat::R8G8_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8_UINT), ResourceFormat::R8G8_UINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R8G8_SINT), ResourceFormat::R8G8_SINT);
  for (ResourceFormat Format :
       {ResourceFormat::R8G8_UNORM, ResourceFormat::R8G8_SNORM,
        ResourceFormat::R8G8_UINT, ResourceFormat::R8G8_SINT})
    EXPECT_EQ(formatElementSize(Format), 2u);
}

TEST(FormatTest, MapsSingleChannelR16Formats) {
  // Roadmap H19n: the single-channel `R16` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16_SFLOAT), ResourceFormat::R16_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16_UNORM), ResourceFormat::R16_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16_SNORM), ResourceFormat::R16_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16_UINT), ResourceFormat::R16_UINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16_SINT), ResourceFormat::R16_SINT);
  for (ResourceFormat Format :
       {ResourceFormat::R16_FLOAT, ResourceFormat::R16_UNORM,
        ResourceFormat::R16_SNORM, ResourceFormat::R16_UINT,
        ResourceFormat::R16_SINT})
    EXPECT_EQ(formatElementSize(Format), 2u);
}

TEST(FormatTest, RejectsUnsupportedFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGB_UNORM_BLOCK), std::nullopt);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_UNDEFINED), std::nullopt);
}

TEST(FormatTest, MapsASTCLDRFormats) {
  // Roadmap E20: all 14 LDR-only ASTC block footprints, each as a
  // `_UNORM`/`_SRGB` pair.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_4x4_UNORM_BLOCK),
            ResourceFormat::ASTC_4x4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_4x4_SRGB_BLOCK),
            ResourceFormat::ASTC_4x4_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x4_UNORM_BLOCK),
            ResourceFormat::ASTC_5x4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x4_SRGB_BLOCK),
            ResourceFormat::ASTC_5x4_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x5_UNORM_BLOCK),
            ResourceFormat::ASTC_5x5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x5_SRGB_BLOCK),
            ResourceFormat::ASTC_5x5_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x5_UNORM_BLOCK),
            ResourceFormat::ASTC_6x5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x5_SRGB_BLOCK),
            ResourceFormat::ASTC_6x5_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x6_UNORM_BLOCK),
            ResourceFormat::ASTC_6x6_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x6_SRGB_BLOCK),
            ResourceFormat::ASTC_6x6_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x5_UNORM_BLOCK),
            ResourceFormat::ASTC_8x5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x5_SRGB_BLOCK),
            ResourceFormat::ASTC_8x5_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x6_UNORM_BLOCK),
            ResourceFormat::ASTC_8x6_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x6_SRGB_BLOCK),
            ResourceFormat::ASTC_8x6_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x8_UNORM_BLOCK),
            ResourceFormat::ASTC_8x8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x8_SRGB_BLOCK),
            ResourceFormat::ASTC_8x8_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x5_UNORM_BLOCK),
            ResourceFormat::ASTC_10x5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x5_SRGB_BLOCK),
            ResourceFormat::ASTC_10x5_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x6_UNORM_BLOCK),
            ResourceFormat::ASTC_10x6_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x6_SRGB_BLOCK),
            ResourceFormat::ASTC_10x6_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x8_UNORM_BLOCK),
            ResourceFormat::ASTC_10x8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x8_SRGB_BLOCK),
            ResourceFormat::ASTC_10x8_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x10_UNORM_BLOCK),
            ResourceFormat::ASTC_10x10_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x10_SRGB_BLOCK),
            ResourceFormat::ASTC_10x10_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x10_UNORM_BLOCK),
            ResourceFormat::ASTC_12x10_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x10_SRGB_BLOCK),
            ResourceFormat::ASTC_12x10_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x12_UNORM_BLOCK),
            ResourceFormat::ASTC_12x12_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x12_SRGB_BLOCK),
            ResourceFormat::ASTC_12x12_SRGB);

  // ASTC HDR-only formats (`_SFLOAT_BLOCK_EXT`, roadmap E21): a single
  // `_SFLOAT` variant per footprint, no separate sRGB pair (HDR data has
  // no sRGB curve to apply).
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_4x4_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_5x4_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_5x5_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_6x5_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_6x6_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_8x5_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_8x6_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_8x8_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_10x5_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_10x6_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_10x8_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_10x10_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_12x10_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT),
            ResourceFormat::ASTC_12x12_SFLOAT);
}

TEST(FormatTest, ElementSizeMatchesFormatWidth) {
  EXPECT_EQ(formatElementSize(ResourceFormat::R32_FLOAT), 4u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32_FLOAT), 8u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32_FLOAT), 12u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R32G32B32A32_FLOAT), 16u);
  EXPECT_EQ(formatElementSize(ResourceFormat::R8G8B8A8_UNORM), 4u);
  EXPECT_EQ(formatElementSize(ResourceFormat::Unknown), 0u);
  // Roadmap E5.
  EXPECT_EQ(formatElementSize(ResourceFormat::A8_UNORM), 1u);
  EXPECT_EQ(formatElementSize(ResourceFormat::A1B5G5R5_UNORM), 2u);
  // Roadmap E19: `VK_EXT_4444_formats`'s two new packed formats.
  EXPECT_EQ(formatElementSize(ResourceFormat::A4R4G4B4_UNORM), 2u);
  EXPECT_EQ(formatElementSize(ResourceFormat::A4B4G4R4_UNORM), 2u);
  // Roadmap E20: block-compressed formats have no per-texel size.
  EXPECT_EQ(formatElementSize(ResourceFormat::ASTC_4x4_UNORM), 0u);
  // Roadmap E21: same for the HDR-only variants.
  EXPECT_EQ(formatElementSize(ResourceFormat::ASTC_4x4_SFLOAT), 0u);
}

TEST(FormatTest, BlockDimensionsMatchASTCFootprint) {
  // Roadmap E20: every ASTC footprint packs into a 16-byte (128-bit) block
  // regardless of its width/height.
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_4x4_UNORM), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_4x4_UNORM), 4u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_4x4_UNORM), 16u);

  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_5x4_SRGB), 5u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_5x4_SRGB), 4u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_5x4_SRGB), 16u);

  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_12x12_SRGB), 12u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_12x12_SRGB), 12u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_12x12_SRGB), 16u);

  // Roadmap E21: the HDR-only variants share the same footprint/block-size
  // rules as their LDR counterparts.
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_4x4_SFLOAT), 4u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_4x4_SFLOAT), 4u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_4x4_SFLOAT), 16u);
  EXPECT_EQ(blockWidth(ResourceFormat::ASTC_12x12_SFLOAT), 12u);
  EXPECT_EQ(blockHeight(ResourceFormat::ASTC_12x12_SFLOAT), 12u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ASTC_12x12_SFLOAT), 16u);

  // A non-block-compressed format is always a 1x1 "block" of its own
  // per-texel size, so callers never need to special-case it.
  EXPECT_EQ(blockWidth(ResourceFormat::R8G8B8A8_UNORM), 1u);
  EXPECT_EQ(blockHeight(ResourceFormat::R8G8B8A8_UNORM), 1u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::R8G8B8A8_UNORM), 4u);
  EXPECT_EQ(blockWidth(ResourceFormat::Unknown), 1u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::Unknown), 0u);
}

TEST(FormatTest, TexelBufferFormatSupportMatchesRuntimeConversionScope) {
  // The CPU runtime's typed-load/store helpers implement a conversion for
  // exactly these formats (see femeCpuResourceLoadTypedV4F32/V4I32 in
  // feme/runtime/CPU/FeMeRuntimeCPU.c) -- every other format, even one
  // `mapVkFormat` itself recognizes, is not usable in a texel buffer.
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_FLOAT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_SINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_SNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_SINT));

  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::R32_FLOAT));
  EXPECT_FALSE(
      isTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_FLOAT));
  EXPECT_FALSE(
      isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM_SRGB));
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::Unknown));
  // Roadmap E20: block-compressed formats cannot back a texel buffer
  // either -- there is no per-texel conversion to apply.
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::ASTC_4x4_UNORM));
  // Roadmap E21: neither can an HDR-only variant.
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::ASTC_4x4_SFLOAT));
}

TEST(FormatTest, IsBlockCompressedFormatDistinguishesASTC) {
  // Roadmap E20.
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::ASTC_4x4_UNORM));
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::ASTC_12x12_SRGB));
  // Roadmap E21: the HDR-only variants are block-compressed too.
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::ASTC_4x4_SFLOAT));
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::ASTC_12x12_SFLOAT));
  EXPECT_FALSE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_FALSE(feme::cpu::isBlockCompressedFormat(ResourceFormat::Unknown));
}

TEST(FormatTest, FormatFeatureFlagsRejectsUnknownFormat) {
  // Roadmap E24.
  EXPECT_EQ(formatFeatureFlags(ResourceFormat::Unknown),
            VkFormatFeatureFlags(0));
}

TEST(FormatTest, FormatFeatureFlagsEveryRecognizedFormatTransfers) {
  // Roadmap E24: `vkCmdCopyImage`/`vkCmdCopyBufferToImage`/
  // `vkCmdCopyImageToBuffer` never convert values and address every
  // recognized format -- block-compressed included -- a whole texel/block
  // at a time (roadmap E22), so every one is a legal transfer source and
  // destination.
  for (ResourceFormat Format :
       {ResourceFormat::R32_FLOAT, ResourceFormat::R8G8B8A8_UNORM,
        ResourceFormat::D32_FLOAT, ResourceFormat::ASTC_4x4_UNORM,
        ResourceFormat::ASTC_4x4_SFLOAT}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  }
}

TEST(FormatTest, FormatFeatureFlagsSampledImageMatchesRuntimeUnpackScope) {
  // Roadmap E25: every non-integer, non-block-compressed,
  // non-depth/stencil format the CPU runtime's texel-unpack table
  // (femeRTImageFormatElementSize/femeRTUnpackImageTexel, FeMeRuntimeCPU.c)
  // now implements, plus every ASTC LDR format (bridged to one of those by
  // materializeImageDescriptor, roadmap E23), can actually be sampled --
  // with filtering. Roadmap H19h adds `R16G16B16A16_{UNORM,SNORM}`, newly
  // implemented (both for sampling and storage-image writes) alongside
  // this row's own storage-image format-breadth work.
  for (ResourceFormat Format :
       {ResourceFormat::R32_FLOAT, ResourceFormat::R32G32_FLOAT,
        ResourceFormat::R32G32B32_FLOAT, ResourceFormat::R32G32B32A32_FLOAT,
        ResourceFormat::R8G8B8A8_UNORM, ResourceFormat::R8G8B8A8_SNORM,
        ResourceFormat::R8G8B8A8_UNORM_SRGB,
        ResourceFormat::R16G16B16A16_FLOAT,
        ResourceFormat::R16G16B16A16_UNORM,
        ResourceFormat::R16G16B16A16_SNORM, ResourceFormat::R11G11B10_FLOAT,
        ResourceFormat::R10G10B10A2_UNORM, ResourceFormat::B8G8R8A8_UNORM,
        ResourceFormat::A8_UNORM, ResourceFormat::A1B5G5R5_UNORM,
        ResourceFormat::ASTC_4x4_UNORM, ResourceFormat::ASTC_12x12_SRGB,
        // Roadmap H19j: `R8_UNORM`/`_SNORM`.
        ResourceFormat::R8_UNORM, ResourceFormat::R8_SNORM,
        // Roadmap H19n: `R8G8_UNORM`/`_SNORM`.
        ResourceFormat::R8G8_UNORM, ResourceFormat::R8G8_SNORM,
        // Roadmap H19n: `R16_FLOAT`/`_UNORM`/`_SNORM`.
        ResourceFormat::R16_FLOAT, ResourceFormat::R16_UNORM,
        ResourceFormat::R16_SNORM}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  }
  // Roadmap E26: the mandatory-sampled `_UINT`/`_SINT` formats
  // femeRTUnpackImageTexelI32 (FeMeRuntimeCPU.c) now decodes can also be
  // sampled, but never with filtering -- SPIR-V never legalizes a filtered
  // `OpImageSample*` against an integer-sampled image, only an unfiltered
  // `OpImageFetch`.
  for (ResourceFormat Format :
       {ResourceFormat::R32G32B32A32_UINT, ResourceFormat::R32G32B32A32_SINT,
        ResourceFormat::R8G8B8A8_UINT, ResourceFormat::R8G8B8A8_SINT,
        ResourceFormat::R16G16B16A16_UINT, ResourceFormat::R16G16B16A16_SINT,
        ResourceFormat::R10G10B10A2_UINT,
        // Roadmap H19j: `R8_UINT`/`_SINT`.
        ResourceFormat::R8_UINT, ResourceFormat::R8_SINT,
        // Roadmap H19n: `R8G8_UINT`/`_SINT`.
        ResourceFormat::R8G8_UINT, ResourceFormat::R8G8_SINT,
        // Roadmap H19n: `R16_UINT`/`_SINT`.
        ResourceFormat::R16_UINT, ResourceFormat::R16_SINT}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  }
  // An HDR ASTC format samples as all-zero (the RGBA8 bridge is LDR-only),
  // a depth format is never a color-sampled format at all, and
  // `R32_UINT`/`_SINT`'s partial-component siblings are not mandatory-
  // sampled and so remain unimplemented (roadmap E26's own scope note,
  // FeMeRuntimeCPU.c) -- all three are honestly left unset, same as every
  // other unimplemented sampled format.
  for (ResourceFormat Format :
       {ResourceFormat::R32_UINT, ResourceFormat::R32G32_UINT,
        ResourceFormat::D32_FLOAT, ResourceFormat::ASTC_4x4_SFLOAT}) {
    EXPECT_FALSE(formatFeatureFlags(Format) &
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  }
}

TEST(FormatTest, FormatFeatureFlags4444FormatsAreTransferAndBlitOnly) {
  // Roadmap E19: `VK_EXT_4444_formats`'s two new formats are recognized
  // `VkFormat` values (so a copy/blit -- which never converts values --
  // can address them a whole texel at a time), but neither is backed by
  // a `feme::graphics::packClearColor`/`unpackColor` case yet, so the
  // sampled-image and color-attachment bits are honestly left unset,
  // same as any other recognized-but-unbacked format.
  for (ResourceFormat Format :
       {ResourceFormat::A4R4G4B4_UNORM, ResourceFormat::A4B4G4R4_UNORM}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  }
}

TEST(FormatTest, FormatFeatureFlagsOnlyAdvertisesStorageImageForTheMandatoryFloor) {
  // Roadmap H19a: a real `feme.cpu.image.store.2d.*` runtime helper now
  // exists (`femeRTStoreTexel2D`/`I32`, FeMeRuntimeCPU.c), but only for
  // exactly the Vulkan spec's own mandatory storage-image format floor --
  // `R32_{SFLOAT,UINT,SINT}`/`R32G32B32A32_{SFLOAT,UINT,SINT}` -- so
  // `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` is set for those. Roadmap H19f
  // widened the same pack helpers (and this bit) to also cover
  // `R16G16B16A16_{SFLOAT,UINT,SINT}`; roadmap H19h added
  // `R16G16B16A16_{UNORM,SNORM}`; roadmap H19j added
  // `R8_{UNORM,SNORM,UINT,SINT}`; roadmap H19n adds
  // `R8G8_{UNORM,SNORM,UINT,SINT}` and
  // `R16_{FLOAT,UNORM,SNORM,UINT,SINT}` -- further slices of the full
  // `shaderStorageImageExtendedFormats` list; every other format is still
  // left unset (`R8G8B8A8_UNORM` included), matching
  // `shaderStorageImageExtendedFormats` staying unclaimed (see Roadmap.md's
  // H19j and its own follow-on rows).
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32B32A32_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32B32A32_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32B32A32_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16B16A16_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16B16A16_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16B16A16_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16B16A16_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16B16A16_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
              VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::R32G32_FLOAT) &
              VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
}

TEST(FormatTest, FormatFeatureFlagsAttachmentBitsMatchRenderPassSupport) {
  // Roadmap E24: matches RenderPass.cpp's own
  // `isSupportedColorAttachmentFormat`/`isSupportedDepthAttachmentFormat`/
  // `isSupportedStencilAttachmentFormat`, which `vkCreateRenderPass` itself
  // already gates on.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::D32_FLOAT) &
              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::S8_UINT) &
              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::R32G32B32A32_UINT) &
               VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::D32_FLOAT) &
               VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
}

TEST(FormatTest, FormatFeatureFlagsBlitBitsMatchImageOpsRejections) {
  // Roadmap E24: `ImageOps.cpp`'s `runBlitImage` rejects a
  // block-compressed *destination* outright and an HDR ASTC *source*, but
  // accepts an LDR ASTC source and every non-block-compressed format
  // either way.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
              VK_FORMAT_FEATURE_BLIT_DST_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::ASTC_4x4_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::ASTC_4x4_UNORM) &
               VK_FORMAT_FEATURE_BLIT_DST_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::ASTC_4x4_SFLOAT) &
               VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::ASTC_4x4_SFLOAT) &
               VK_FORMAT_FEATURE_BLIT_DST_BIT);
}

} // namespace
