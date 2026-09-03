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

// (Roadmap H8r) `VK_FORMAT_B8G8R8A8_SRGB`, split off from H8g's own
// mandatory blit/filter bits audit as an entirely unmapped format (unlike
// H8g's own genuine scope, which found only under-reported bits, this one
// had no `mapVkFormat` case at all).
TEST(FormatTest, MapsBGRA8SRGBFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_B8G8R8A8_SRGB),
            ResourceFormat::B8G8R8A8_UNORM_SRGB);
  EXPECT_EQ(formatElementSize(ResourceFormat::B8G8R8A8_UNORM_SRGB), 4u);
}

// (Roadmap H8r) `B8G8R8A8_UNORM_SRGB` needs every one of the CTS's own
// mandatory `format_properties` bits for `b8g8r8a8_srgb`, since it was
// previously reporting *none* of them (an entirely unmapped format, not a
// merely-under-reported one).
TEST(FormatTest, FormatFeatureFlagsBGRA8SRGBMatchesCTSMandatoryBits) {
  VkFormatFeatureFlags Flags =
      formatFeatureFlags(ResourceFormat::B8G8R8A8_UNORM_SRGB);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_DST_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
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

TEST(FormatTest, MapsTwoChannelR16G16Formats) {
  // Roadmap H19n: the two-channel `R16G16` mandatory
  // `shaderStorageImageExtendedFormats` formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16G16_SFLOAT), ResourceFormat::R16G16_FLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16G16_UNORM), ResourceFormat::R16G16_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16G16_SNORM), ResourceFormat::R16G16_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16G16_UINT), ResourceFormat::R16G16_UINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_R16G16_SINT), ResourceFormat::R16G16_SINT);
  for (ResourceFormat Format :
       {ResourceFormat::R16G16_FLOAT, ResourceFormat::R16G16_UNORM,
        ResourceFormat::R16G16_SNORM, ResourceFormat::R16G16_UINT,
        ResourceFormat::R16G16_SINT})
    EXPECT_EQ(formatElementSize(Format), 4u);
}

TEST(FormatTest, MapsSignedPacked32BitFormats) {
  // Roadmap H19o: the final two mandatory
  // `shaderStorageImageExtendedFormats` formats -- the signed siblings of
  // `R10G10B10A2_{UNORM,UINT}`, same MSB-down `A2B10G10R10` bit layout,
  // packed into the same single 4-byte word.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A2B10G10R10_SNORM_PACK32),
           ResourceFormat::R10G10B10A2_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A2B10G10R10_SINT_PACK32),
           ResourceFormat::R10G10B10A2_SINT);
  for (ResourceFormat Format :
       {ResourceFormat::R10G10B10A2_SNORM, ResourceFormat::R10G10B10A2_SINT})
    EXPECT_EQ(formatElementSize(Format), 4u);
}

TEST(FormatTest, RejectsUnsupportedFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG), std::nullopt);
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

TEST(FormatTest, MapsBCFormats) {
  // Roadmap H8n: all 16 `VK_FORMAT_BC*` block footprints.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGB_UNORM_BLOCK),
            ResourceFormat::BC1_RGB_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGB_SRGB_BLOCK),
            ResourceFormat::BC1_RGB_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK),
            ResourceFormat::BC1_RGBA_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC1_RGBA_SRGB_BLOCK),
            ResourceFormat::BC1_RGBA_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC2_UNORM_BLOCK), ResourceFormat::BC2_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC2_SRGB_BLOCK), ResourceFormat::BC2_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC3_UNORM_BLOCK), ResourceFormat::BC3_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC3_SRGB_BLOCK), ResourceFormat::BC3_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC4_UNORM_BLOCK), ResourceFormat::BC4_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC4_SNORM_BLOCK), ResourceFormat::BC4_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC5_UNORM_BLOCK), ResourceFormat::BC5_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC5_SNORM_BLOCK), ResourceFormat::BC5_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC6H_UFLOAT_BLOCK),
            ResourceFormat::BC6H_UFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC6H_SFLOAT_BLOCK),
            ResourceFormat::BC6H_SFLOAT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC7_UNORM_BLOCK), ResourceFormat::BC7_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_BC7_SRGB_BLOCK), ResourceFormat::BC7_SRGB);
}

TEST(FormatTest, MapsETC2Formats) {
  // Roadmap H8j: all 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` formats.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK),
            ResourceFormat::ETC2_RGB8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK),
            ResourceFormat::ETC2_RGB8_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK),
            ResourceFormat::ETC2_RGB8A1_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK),
            ResourceFormat::ETC2_RGB8A1_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK),
            ResourceFormat::ETC2_RGBA8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK),
            ResourceFormat::ETC2_RGBA8_SRGB);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_EAC_R11_UNORM_BLOCK),
            ResourceFormat::EAC_R11_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_EAC_R11_SNORM_BLOCK),
            ResourceFormat::EAC_R11_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_EAC_R11G11_UNORM_BLOCK),
            ResourceFormat::EAC_R11G11_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_EAC_R11G11_SNORM_BLOCK),
            ResourceFormat::EAC_R11G11_SNORM);
}

// (Roadmap H8q) `VK_FORMAT_E5B9G9R9_UFLOAT_PACK32`: an entirely new
// shared-exponent packed format, unlike every other recognized format
// above -- a real `deqp-vk` run found it missing every feature bit
// (`BLIT_SRC_BIT`/`SAMPLED_IMAGE_BIT`/`FILTER_LINEAR_BIT`/
// `TRANSFER_DST_BIT`/`TRANSFER_SRC_BIT`) since `mapVkFormat` had no case
// for it at all.
TEST(FormatTest, MapsE5B9G9R9UfloatFormat) {
  EXPECT_EQ(mapVkFormat(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32),
            ResourceFormat::E5B9G9R9_UFLOAT);
  // Packed into a single 4-byte word, the same as `R11G11B10_FLOAT`.
  EXPECT_EQ(formatElementSize(ResourceFormat::E5B9G9R9_UFLOAT), 4u);
  // `TRANSFER_SRC_BIT`/`TRANSFER_DST_BIT` are granted unconditionally to
  // any recognized, non-block-compressed format; `BLIT_SRC_BIT`/
  // `BLIT_DST_BIT` likewise for any non-block-compressed format --
  // automatic once `mapVkFormat` recognizes the format, no dedicated
  // case needed for any of these four.
  VkFormatFeatureFlags Flags =
      formatFeatureFlags(ResourceFormat::E5B9G9R9_UFLOAT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_BLIT_DST_BIT);
  // `SAMPLED_IMAGE_BIT`/`FILTER_LINEAR_BIT` need the new dedicated
  // `femeRTUnpackImageTexel` case (`femeRTUnpackRGB9E5`,
  // FeMeRuntimeCPU.c) this row adds.
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
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
  // Roadmap H8n: same for every BC format.
  EXPECT_EQ(formatElementSize(ResourceFormat::BC1_RGB_UNORM), 0u);
  EXPECT_EQ(formatElementSize(ResourceFormat::BC7_SRGB), 0u);
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

TEST(FormatTest, BlockDimensionsMatchBCFootprint) {
  // Roadmap H8n: every BC format shares the same 4x4 texel footprint, but
  // BC1/BC4 pack into an 8-byte (64-bit) block while every other BC
  // format packs into a 16-byte (128-bit) block.
  for (ResourceFormat Format :
       {ResourceFormat::BC1_RGB_UNORM, ResourceFormat::BC1_RGB_SRGB,
        ResourceFormat::BC1_RGBA_UNORM, ResourceFormat::BC1_RGBA_SRGB,
        ResourceFormat::BC2_UNORM, ResourceFormat::BC2_SRGB,
        ResourceFormat::BC3_UNORM, ResourceFormat::BC3_SRGB,
        ResourceFormat::BC4_UNORM, ResourceFormat::BC4_SNORM,
        ResourceFormat::BC5_UNORM, ResourceFormat::BC5_SNORM,
        ResourceFormat::BC6H_UFLOAT, ResourceFormat::BC6H_SFLOAT,
        ResourceFormat::BC7_UNORM, ResourceFormat::BC7_SRGB}) {
    EXPECT_EQ(blockWidth(Format), 4u);
    EXPECT_EQ(blockHeight(Format), 4u);
  }
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC1_RGB_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC1_RGBA_SRGB), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC4_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC4_SNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC2_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC3_SRGB), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC5_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC6H_UFLOAT), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC6H_SFLOAT), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::BC7_UNORM), 16u);
}

TEST(FormatTest, BlockDimensionsMatchETC2Footprint) {
  // Roadmap H8j: every ETC2/EAC format shares the same 4x4 texel
  // footprint as BC, but only the single-64-bit-plane formats
  // (ETC2_RGB8_*, ETC2_RGB8A1_*, EAC_R11_*) pack into an 8-byte block --
  // the dual-64-bit-plane formats (ETC2_RGBA8_*, EAC_R11G11_*, each
  // storing two independent halves) pack into a 16-byte block.
  for (ResourceFormat Format :
       {ResourceFormat::ETC2_RGB8_UNORM, ResourceFormat::ETC2_RGB8_SRGB,
        ResourceFormat::ETC2_RGB8A1_UNORM,
        ResourceFormat::ETC2_RGB8A1_SRGB, ResourceFormat::ETC2_RGBA8_UNORM,
        ResourceFormat::ETC2_RGBA8_SRGB, ResourceFormat::EAC_R11_UNORM,
        ResourceFormat::EAC_R11_SNORM, ResourceFormat::EAC_R11G11_UNORM,
        ResourceFormat::EAC_R11G11_SNORM}) {
    EXPECT_EQ(blockWidth(Format), 4u);
    EXPECT_EQ(blockHeight(Format), 4u);
  }
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGB8_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGB8A1_SRGB), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11_UNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11_SNORM), 8u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGBA8_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::ETC2_RGBA8_SRGB), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11G11_UNORM), 16u);
  EXPECT_EQ(bytesPerBlock(ResourceFormat::EAC_R11G11_SNORM), 16u);
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
  // (Roadmap L9) The single-channel 32-bit identity formats
  // (`RWBuffer<float>`/`RWBuffer<int>`/`RWBuffer<uint>`'s own shape), now
  // that `femeCpuResourceLoadTypedF32`/`StoreTypedF32`/`...I32` implement a
  // conversion for them too.
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32_FLOAT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32_SINT));
  // (Roadmap H8d) A real `dEQP-VK.api.info.format_properties.*` re-run
  // found 21 further mandatory `UNIFORM_TEXEL_BUFFER_BIT` formats, all
  // now reachable through the generic-table-based
  // `femeCpuResourceLoadTypedV4F32`/`V4I32` refactor (see
  // `femeRTImageFormatElementSize`/`UnpackImageTexel(I32)` in
  // FeMeRuntimeCPU.c).
  EXPECT_TRUE(
      isTexelBufferFormatSupported(ResourceFormat::R10G10B10A2_UNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R10G10B10A2_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R11G11B10_FLOAT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::B8G8R8A8_UNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R16_FLOAT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R16_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R16_SINT));
  EXPECT_TRUE(
      isTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_FLOAT));
  EXPECT_TRUE(
      isTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_UINT));
  EXPECT_TRUE(
      isTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_SINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32_FLOAT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R32G32_SINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8_UNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8_SNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8_SINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8_UNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8_SNORM));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R8G8_SINT));
  // (Roadmap H8s) `R16G16_UINT`/`_SINT`: a real CTS re-run found this pair
  // still missing `UNIFORM_TEXEL_BUFFER_BIT`, a plain omission from H8d's
  // own two-channel-16-bit coverage above.
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R16G16_UINT));
  EXPECT_TRUE(isTexelBufferFormatSupported(ResourceFormat::R16G16_SINT));

  EXPECT_FALSE(
      isTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM_SRGB));
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::Unknown));
  // Roadmap E20: block-compressed formats cannot back a texel buffer
  // either -- there is no per-texel conversion to apply.
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::ASTC_4x4_UNORM));
  // Roadmap E21: neither can an HDR-only variant.
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::ASTC_4x4_SFLOAT));
  // Roadmap H8n: nor can a BC format.
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_FALSE(isTexelBufferFormatSupported(ResourceFormat::BC7_SRGB));
}

TEST(FormatTest, StorageTexelBufferFormatSupportIsNarrowerThanReadOnlyScope) {
  // (Roadmap H8d) The original 10-format scope is read+write-capable, so
  // `isStorageTexelBufferFormatSupported` still reports every one of them.
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_FLOAT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_UINT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R32G32B32A32_SINT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_SNORM));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_UINT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R8G8B8A8_SINT));
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(ResourceFormat::R32_FLOAT));
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(ResourceFormat::R32_UINT));
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(ResourceFormat::R32_SINT));

  // Of the 21 new `isTexelBufferFormatSupported` formats, only these 6
  // are also real mandatory `STORAGE_TEXEL_BUFFER_BIT` entries (a real
  // CTS re-run confirms this, not spec prose alone).
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(
      ResourceFormat::R16G16B16A16_FLOAT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_UINT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R16G16B16A16_SINT));
  EXPECT_TRUE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R32G32_FLOAT));
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(ResourceFormat::R32G32_UINT));
  EXPECT_TRUE(isStorageTexelBufferFormatSupported(ResourceFormat::R32G32_SINT));

  // The rest of the 21 stay uniform-only: no `femeRTPackImageTexel(I32)`
  // write support exists for them (or, for `B8G8R8A8_UNORM`, deliberately
  // never has -- see its own doc comment in Format.h).
  EXPECT_FALSE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R10G10B10A2_UNORM));
  EXPECT_FALSE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R11G11B10_FLOAT));
  EXPECT_FALSE(
      isStorageTexelBufferFormatSupported(ResourceFormat::B8G8R8A8_UNORM));
  EXPECT_FALSE(isStorageTexelBufferFormatSupported(ResourceFormat::R16_FLOAT));
  EXPECT_FALSE(isStorageTexelBufferFormatSupported(ResourceFormat::R8_UNORM));
  EXPECT_FALSE(
      isStorageTexelBufferFormatSupported(ResourceFormat::R8G8_UNORM));
  EXPECT_FALSE(isStorageTexelBufferFormatSupported(ResourceFormat::Unknown));
}


TEST(FormatTest, VertexBufferFormatSupportMatchesDecodeAttributeScope) {
  // (Roadmap H8/H8b) `Executor.cpp`'s `decodeAttribute` implements exactly
  // these formats' vertex-attribute decode -- `isVertexBufferFormatSupported`
  // must report the same set `GraphicsPipeline.cpp`'s
  // `isSupportedVertexAttributeFormat` (which now forwards here) and
  // `vkGetPhysicalDeviceFormatProperties`'s own `bufferFeatures` both rely
  // on.
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32_FLOAT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32_FLOAT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32B32_FLOAT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R32G32B32A32_FLOAT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32B32_UINT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R32G32B32A32_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R32G32B32_SINT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R32G32B32A32_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R8G8B8A8_UNORM_SRGB));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8B8A8_SNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8B8A8_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8B8A8_SINT));
  // (Roadmap H8b) The single- and two-channel 8-bit-per-component families.
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8_UNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8_SNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8_UNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8_SNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R8G8_SINT));
  // (Roadmap H8b) The 16-bit-per-component families, including `_FLOAT`.
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16_UNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16_SNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16_FLOAT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16G16_UNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16G16_SNORM));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16G16_UINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16G16_SINT));
  EXPECT_TRUE(isVertexBufferFormatSupported(ResourceFormat::R16G16_FLOAT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R16G16B16A16_UNORM));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R16G16B16A16_SNORM));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R16G16B16A16_UINT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R16G16B16A16_SINT));
  EXPECT_TRUE(
      isVertexBufferFormatSupported(ResourceFormat::R16G16B16A16_FLOAT));

  // Formats `decodeAttribute` does not implement yet (the packed
  // `A2B10G10R10_UNORM_PACK32`, tracked as its own remaining roadmap H8
  // follow-on row since it does not fit `decodeAttribute`'s "N bytes per
  // component" convention mechanically): not yet a supported vertex
  // attribute format, even though it is mandatory per the Vulkan spec.
  EXPECT_FALSE(
      isVertexBufferFormatSupported(ResourceFormat::R10G10B10A2_UNORM));
  EXPECT_FALSE(isVertexBufferFormatSupported(ResourceFormat::B8G8R8A8_UNORM));
  EXPECT_FALSE(isVertexBufferFormatSupported(ResourceFormat::Unknown));
  EXPECT_FALSE(isVertexBufferFormatSupported(ResourceFormat::ASTC_4x4_UNORM));
  // Roadmap H8n: nor can a BC format.
  EXPECT_FALSE(isVertexBufferFormatSupported(ResourceFormat::BC1_RGB_UNORM));
}

TEST(FormatTest, MapsA8B8G8R8Pack32FormatsOntoR8G8B8A8) {
  // (Roadmap H8) `VK_FORMAT_A8B8G8R8_*_PACK32` is byte-for-byte identical
  // in memory to `R8G8B8A8_*` (R in byte 0 .. A in byte 3 for both), so
  // these four packed `VkFormat` enum values -- previously entirely
  // unmapped -- reuse the exact same `ResourceFormat` value.
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8B8G8R8_UNORM_PACK32),
            ResourceFormat::R8G8B8A8_UNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8B8G8R8_SNORM_PACK32),
            ResourceFormat::R8G8B8A8_SNORM);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8B8G8R8_UINT_PACK32),
            ResourceFormat::R8G8B8A8_UINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8B8G8R8_SINT_PACK32),
            ResourceFormat::R8G8B8A8_SINT);
  EXPECT_EQ(mapVkFormat(VK_FORMAT_A8B8G8R8_SRGB_PACK32),
            ResourceFormat::R8G8B8A8_UNORM_SRGB);
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

TEST(FormatTest, IsBlockCompressedFormatDistinguishesBC) {
  // Roadmap H8n: every one of the 16 `VK_FORMAT_BC*` formats is block-
  // compressed too, and `isBCFormat`/`isASTCFormat` partition that same
  // predicate into its two disjoint families.
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_TRUE(feme::cpu::isBlockCompressedFormat(ResourceFormat::BC7_SRGB));
  EXPECT_TRUE(feme::cpu::isBCFormat(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_TRUE(feme::cpu::isBCFormat(ResourceFormat::BC7_SRGB));
  EXPECT_FALSE(feme::cpu::isBCFormat(ResourceFormat::ASTC_4x4_UNORM));
  EXPECT_FALSE(feme::cpu::isASTCFormat(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_FALSE(feme::cpu::isBCFormat(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_FALSE(feme::cpu::isBCFormat(ResourceFormat::Unknown));
  // BC1/BC2/BC3/BC7 decode to RGBA8; BC4/BC5/BC6H do not.
  EXPECT_TRUE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_TRUE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC3_SRGB));
  EXPECT_TRUE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC7_UNORM));
  EXPECT_FALSE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC4_UNORM));
  EXPECT_FALSE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC5_SNORM));
  EXPECT_FALSE(feme::cpu::isBCRGBA8Format(ResourceFormat::BC6H_UFLOAT));
}

TEST(FormatTest, IsBlockCompressedFormatDistinguishesETC2) {
  // Roadmap H8j: every one of the 10 `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*`
  // formats is block-compressed too, and `isETC2Format` partitions that
  // same predicate into a third disjoint family alongside ASTC and BC.
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::ETC2_RGB8_UNORM));
  EXPECT_TRUE(
      feme::cpu::isBlockCompressedFormat(ResourceFormat::EAC_R11G11_SNORM));
  EXPECT_TRUE(feme::cpu::isETC2Format(ResourceFormat::ETC2_RGB8_UNORM));
  EXPECT_TRUE(feme::cpu::isETC2Format(ResourceFormat::EAC_R11G11_SNORM));
  EXPECT_FALSE(feme::cpu::isETC2Format(ResourceFormat::BC1_RGB_UNORM));
  EXPECT_FALSE(feme::cpu::isETC2Format(ResourceFormat::ASTC_4x4_UNORM));
  EXPECT_FALSE(feme::cpu::isETC2Format(ResourceFormat::R8G8B8A8_UNORM));
  EXPECT_FALSE(feme::cpu::isETC2Format(ResourceFormat::Unknown));
  EXPECT_FALSE(feme::cpu::isBCFormat(ResourceFormat::ETC2_RGB8_UNORM));
  EXPECT_FALSE(feme::cpu::isASTCFormat(ResourceFormat::EAC_R11_UNORM));
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
        // Roadmap H8r: `B8G8R8A8_UNORM_SRGB`, an entirely unmapped
        // format H8g's own audit split off (not merely under-reported --
        // `mapVkFormat` had no case at all), now decoded by
        // `femeRTUnpackImageTexel`'s own case (sRGB-decoded like
        // `R8G8B8A8_UNORM_SRGB` above, byte-swapped like
        // `B8G8R8A8_UNORM`).
        ResourceFormat::B8G8R8A8_UNORM_SRGB,
        ResourceFormat::A8_UNORM, ResourceFormat::A1B5G5R5_UNORM,
        // Roadmap H8e: `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM`, a CTS-confirmed
        // genuine `SAMPLED_IMAGE_BIT` gap rather than a reporting-only
        // one, now decoded by `femeRTUnpackImageTexel`'s own cases.
        ResourceFormat::B4G4R4A4_UNORM, ResourceFormat::A1R5G5B5_UNORM,
        // Roadmap H8g: `R5G6B5_UNORM`/`B5G6R5_UNORM`, another
        // CTS-confirmed genuine `SAMPLED_IMAGE_BIT` gap, now decoded by
        // `femeRTUnpackImageTexel`'s own `femeRTUnpackR5G6B5Unorm` case.
        ResourceFormat::R5G6B5_UNORM, ResourceFormat::B5G6R5_UNORM,
        ResourceFormat::ASTC_4x4_UNORM, ResourceFormat::ASTC_12x12_SRGB,
        // Roadmap H19j: `R8_UNORM`/`_SNORM`.
        ResourceFormat::R8_UNORM, ResourceFormat::R8_SNORM,
        // Roadmap H19n: `R8G8_UNORM`/`_SNORM`.
        ResourceFormat::R8G8_UNORM, ResourceFormat::R8G8_SNORM,
        // Roadmap H19n: `R16_FLOAT`/`_UNORM`/`_SNORM`.
        ResourceFormat::R16_FLOAT, ResourceFormat::R16_UNORM,
        ResourceFormat::R16_SNORM,
        // Roadmap H19n: `R16G16_FLOAT`/`_UNORM`/`_SNORM`.
        ResourceFormat::R16G16_FLOAT, ResourceFormat::R16G16_UNORM,
        ResourceFormat::R16G16_SNORM,
        // Roadmap H19o: `R10G10B10A2_SNORM`.
        ResourceFormat::R10G10B10A2_SNORM,
        // Roadmap H8n: every one of the 16 `VK_FORMAT_BC*` formats
        // samples too, via `materializeImageDescriptor`'s own BC decode
        // bridge (mirroring the ASTC LDR bridge above).
        ResourceFormat::BC1_RGB_UNORM, ResourceFormat::BC1_RGBA_SRGB,
        ResourceFormat::BC2_UNORM, ResourceFormat::BC3_SRGB,
        ResourceFormat::BC4_UNORM, ResourceFormat::BC4_SNORM,
        ResourceFormat::BC5_UNORM, ResourceFormat::BC5_SNORM,
        ResourceFormat::BC6H_UFLOAT, ResourceFormat::BC6H_SFLOAT,
        ResourceFormat::BC7_UNORM, ResourceFormat::BC7_SRGB}) {
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
        ResourceFormat::R16_UINT, ResourceFormat::R16_SINT,
        // Roadmap H19n: `R16G16_UINT`/`_SINT`.
        ResourceFormat::R16G16_UINT, ResourceFormat::R16G16_SINT,
        // Roadmap H19o: `R10G10B10A2_SINT`.
        ResourceFormat::R10G10B10A2_SINT,
        // (Roadmap H8s) `R32_{UINT,SINT}`/`R32G32_{UINT,SINT}`: a real
        // CTS re-run found these still missing `SAMPLED_IMAGE_BIT`
        // despite `femeRTUnpackImageTexelI32` already decoding all four
        // (roadmap H19a) -- a plain omission from this switch, unlike
        // E26's own comment above (written before that gap was found).
        ResourceFormat::R32_UINT, ResourceFormat::R32_SINT,
        ResourceFormat::R32G32_UINT, ResourceFormat::R32G32_SINT}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  }
  // An HDR ASTC format samples as all-zero (the RGBA8 bridge is LDR-only)
  // and a depth format is never a color-sampled format at all -- both are
  // honestly left unset, same as every other unimplemented sampled
  // format.
  for (ResourceFormat Format :
       {ResourceFormat::D32_FLOAT, ResourceFormat::ASTC_4x4_SFLOAT}) {
    EXPECT_FALSE(formatFeatureFlags(Format) &
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  }
  // Roadmap H8e: `D16_UNORM`'s own missing `SAMPLED_IMAGE_BIT` is a
  // genuine gap, not a reporting-only one -- unlike `D32_FLOAT` above,
  // the real mandatory-format-table `deqp-vk` run this row's own
  // investigation used does require it, and `femeRTFetchTexel2D` already
  // decodes `D16_UNORM` (roadmap F8b), so this is just the missing
  // advertisement. No `_FILTER_LINEAR_BIT`: that same CTS run does not
  // require it for `d16_unorm`.
  {
    VkFormatFeatureFlags Flags = formatFeatureFlags(ResourceFormat::D16_UNORM);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
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
  // `R8G8_{UNORM,SNORM,UINT,SINT}`,
  // `R16_{FLOAT,UNORM,SNORM,UINT,SINT}`,
  // `R16G16_{FLOAT,UNORM,SNORM,UINT,SINT}`,
  // `R32G32_{UINT,SINT}`, the packed 32-bit formats
  // `A2B10G10R10_{UNORM,UINT}_PACK32`/`B10G11R11_UFLOAT_PACK32`, and
  // `R8G8B8A8_{SNORM,SINT}` (a real mandatory entry discovered via the
  // Vulkan spec's own full mandatory list, distinct from
  // `R8G8B8A8_{UNORM,UINT}` staying unclaimed); roadmap H19o adds the
  // final two, `R10G10B10A2_{SNORM,SINT}`, completing the real Vulkan
  // spec's own full mandatory `shaderStorageImageExtendedFormats` list.
  // Every other format is still left unset (`R8G8B8A8_UNORM` included),
  // even though the mandatory list itself is now complete -- see
  // `PhysicalDeviceInfo.cpp` for the actual device-feature-bit flip and
  // Roadmap.md's H19o row for the CTS re-run that justifies it.
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
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R16G16_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  // (Roadmap H19n) `R32G32_UINT`/`R32G32_SINT`: the storage-mandatory
  // two-component partial siblings of `R32G32B32A32_{UINT,SINT}`.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  // (Roadmap H19n) The packed 32-bit formats
  // `A2B10G10R10_{UNORM,UINT}_PACK32`/`B10G11R11_UFLOAT_PACK32`.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R11G11B10_FLOAT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R10G10B10A2_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R10G10B10A2_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  // (Roadmap H19o) `R10G10B10A2_{SNORM,SINT}`: the final two formats in
  // the real Vulkan spec's own mandatory `shaderStorageImageExtendedFormats`
  // list -- with these, the full mandatory list is now complete.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R10G10B10A2_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R10G10B10A2_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  // (Roadmap H19n) `R8G8B8A8_SNORM`/`_SINT`: a real mandatory entry
  // distinct from `R8G8B8A8_UNORM`/`_UINT`.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_SNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_SINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  // (Roadmap H8s) `R8G8B8A8_{UINT,UNORM}`/`R32G32_FLOAT`: a real CTS
  // re-run found these three still missing `STORAGE_IMAGE_BIT`, part of
  // Vulkan's own mandatory storage-image floor -- `femeRTPackImageTexel`/
  // `PackImageTexelI32` (FeMeRuntimeCPU.c, roadmap H8d) already have real
  // encode cases for all three.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UINT) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R8G8B8A8_UNORM) &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::R32G32_FLOAT) &
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

// (Roadmap H8p) The 7 real integer color-attachment formats get
// `COLOR_ATTACHMENT_BIT` (a real `UInt`/`SInt` fragment output can now be
// drawn to one, `Executor.cpp`'s `executeDraws`/`readFragmentColorInt`),
// but never `COLOR_ATTACHMENT_BLEND_BIT` -- blending is undefined for an
// integer format per spec, confirmed by the real CTS run this row's own
// investigation used.
TEST(FormatTest, IntegerColorAttachmentFormatsGetOnlyColorAttachmentBit) {
  for (ResourceFormat Format :
       {ResourceFormat::R8G8B8A8_UINT, ResourceFormat::R8G8B8A8_SINT,
        ResourceFormat::R10G10B10A2_UINT, ResourceFormat::R16_UINT,
        ResourceFormat::R16_SINT, ResourceFormat::R16G16_UINT,
        ResourceFormat::R16G16_SINT}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        << "format " << static_cast<int>(Format);
    EXPECT_FALSE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        << "format " << static_cast<int>(Format);
  }
}

// (Roadmap H8s) A real CTS re-run found these four non-integer formats
// still missing `COLOR_ATTACHMENT_BIT`/`_BLEND_BIT` -- unlike the integer
// cluster above, blending *is* defined for these (all `Float`-typed), so
// both bits are expected, mirroring every other non-integer
// `isSupportedColorAttachmentFormat` (RenderPass.cpp) format.
TEST(FormatTest, NonIntegerColorAttachmentBreadthGetsBothBits) {
  for (ResourceFormat Format :
       {ResourceFormat::R8_UNORM, ResourceFormat::R8G8_UNORM,
        ResourceFormat::R16_FLOAT, ResourceFormat::R16G16_FLOAT}) {
    VkFormatFeatureFlags Flags = formatFeatureFlags(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        << "format " << static_cast<int>(Format);
    EXPECT_TRUE(Flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        << "format " << static_cast<int>(Format);
  }
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
  // Roadmap H8o: the same holds for BC -- every BC destination is
  // rejected, but every one of the 16 BC formats (BC1-7) is now a legal
  // blit source: `bcSamplingTarget`/`decodeBCBlock` (BCSamplingBridge.h)
  // decode each BC sub-family into whichever already-runtime-supported
  // `ResourceFormat` matches its own shape, and `feme::graphics::
  // unpackColor`/`packClearColor` (ImageFixture.cpp) now have a case for
  // every one of those targets.
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::BC1_RGBA_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::BC1_RGBA_UNORM) &
               VK_FORMAT_FEATURE_BLIT_DST_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::BC7_SRGB) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::BC4_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::BC5_SNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::BC6H_SFLOAT) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  // Roadmap H8j: the same holds for ETC2/EAC -- every ETC2/EAC
  // destination is rejected (`isBlockCompressedFormat` already covers
  // it), but every one of the 10 formats is now a legal blit source:
  // `etc2SamplingTarget`/`decodeETC2FormatBlock` (ETC2SamplingBridge.h)
  // decode each sub-family into whichever already-runtime-supported
  // `ResourceFormat` matches its own shape, and `feme::graphics::
  // unpackColor`/`packClearColor` (ImageFixture.cpp) now have a case for
  // every one of those targets (including the new R16/R16G16 UNORM/SNORM
  // cases roadmap H8j itself added for EAC_R11/EAC_R11G11).
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::ETC2_RGB8_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_FALSE(formatFeatureFlags(ResourceFormat::ETC2_RGB8_UNORM) &
               VK_FORMAT_FEATURE_BLIT_DST_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::ETC2_RGB8A1_SRGB) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::ETC2_RGBA8_SRGB) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::EAC_R11_SNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
  EXPECT_TRUE(formatFeatureFlags(ResourceFormat::EAC_R11G11_UNORM) &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT);
}

} // namespace
