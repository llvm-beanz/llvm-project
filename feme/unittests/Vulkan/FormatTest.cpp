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
  // materializeImageDescriptor, roadmap E23), can actually be sampled.
  for (ResourceFormat Format :
       {ResourceFormat::R32_FLOAT, ResourceFormat::R32G32_FLOAT,
        ResourceFormat::R32G32B32_FLOAT, ResourceFormat::R32G32B32A32_FLOAT,
        ResourceFormat::R8G8B8A8_UNORM, ResourceFormat::R8G8B8A8_SNORM,
        ResourceFormat::R8G8B8A8_UNORM_SRGB,
        ResourceFormat::R16G16B16A16_FLOAT, ResourceFormat::R11G11B10_FLOAT,
        ResourceFormat::R10G10B10A2_UNORM, ResourceFormat::B8G8R8A8_UNORM,
        ResourceFormat::A8_UNORM, ResourceFormat::A1B5G5R5_UNORM,
        ResourceFormat::ASTC_4x4_UNORM, ResourceFormat::ASTC_12x12_SRGB}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  }
  // An HDR ASTC format samples as all-zero (the RGBA8 bridge is LDR-only),
  // and an integer format has no `feme.cpu.image.*` entry point that could
  // consume a decoded value yet (roadmap E25's own scope note,
  // FeMeRuntimeCPU.c), so both are honestly left unset, same as every
  // other unimplemented sampled format.
  for (ResourceFormat Format :
       {ResourceFormat::R32G32B32A32_UINT, ResourceFormat::R8G8B8A8_UINT,
        ResourceFormat::R16G16B16A16_UINT, ResourceFormat::D32_FLOAT,
        ResourceFormat::ASTC_4x4_SFLOAT}) {
    EXPECT_FALSE(formatFeatureFlags(Format) &
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  }
}

TEST(FormatTest, FormatFeatureFlagsNeverAdvertisesStorageImage) {
  // Roadmap E24: no `feme.cpu.image.store.*` runtime helper exists for any
  // format yet (see "V5: Images and sampling" in FeMeVulkanDesign.md), so
  // `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` is never set, even for a format
  // every other feature bit is set for.
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::R32G32B32A32_FLOAT) &
               VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
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
