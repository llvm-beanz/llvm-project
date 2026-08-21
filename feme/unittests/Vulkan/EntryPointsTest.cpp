//===- EntryPointsTest.cpp - vkGetPhysicalDevice*FormatProperties tests -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap E24: `vkGetPhysicalDeviceFormatProperties`/
// `vkGetPhysicalDeviceImageFormatProperties` used to unconditionally report
// "nothing is supported" for every format -- these tests confirm the real
// replacement actually reflects what this ICD implements, since a stale
// stub answer here is exactly what silently failed every
// `dEQP-VK.texture.*` CTS case regardless of ASTC support (roadmap E22's
// own CTS run).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "EntryPoints.h"
#include "Icd.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

class EntryPointsTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
  }
  void TearDown() override { vkDestroyInstance(Instance, nullptr); }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
};

TEST_F(EntryPointsTest, FormatPropertiesRejectsUnrecognizedFormat) {
  VkFormatProperties Props{};
  vkGetPhysicalDeviceFormatProperties(Physical, VK_FORMAT_BC1_RGB_UNORM_BLOCK,
                                      &Props);
  EXPECT_EQ(Props.linearTilingFeatures, VkFormatFeatureFlags(0));
  EXPECT_EQ(Props.optimalTilingFeatures, VkFormatFeatureFlags(0));
  EXPECT_EQ(Props.bufferFeatures, VkFormatFeatureFlags(0));
}

TEST_F(EntryPointsTest, FormatPropertiesReportsSampledAndAttachmentFormat) {
  // `R8G8B8A8_UNORM` is both a sampled-image and Vulkan 1.2 mandatory
  // color-attachment format (RenderPass.cpp's
  // `isSupportedColorAttachmentFormat`).
  VkFormatProperties Props{};
  vkGetPhysicalDeviceFormatProperties(Physical, VK_FORMAT_R8G8B8A8_UNORM,
                                      &Props);
  EXPECT_TRUE(Props.optimalTilingFeatures &
              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  EXPECT_TRUE(Props.optimalTilingFeatures &
              VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  // `VK_IMAGE_TILING_LINEAR`/`_OPTIMAL` are not distinguished anywhere in
  // this ICD (see Image.h's file comment), so both tiling fields match.
  EXPECT_EQ(Props.linearTilingFeatures, Props.optimalTilingFeatures);
  EXPECT_TRUE(Props.bufferFeatures &
              VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT);
  EXPECT_TRUE(Props.bufferFeatures &
              VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT);
}

TEST_F(EntryPointsTest, FormatPropertiesNeverReportsStorageImage) {
  // No format has a shader-writable storage image path yet (see "V5:
  // Images and sampling" in FeMeVulkanDesign.md).
  VkFormatProperties Props{};
  vkGetPhysicalDeviceFormatProperties(Physical, VK_FORMAT_R8G8B8A8_UNORM,
                                      &Props);
  EXPECT_FALSE(Props.optimalTilingFeatures &
               VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
}

TEST_F(EntryPointsTest, FormatProperties2MatchesFormatProperties) {
  VkFormatProperties Props{};
  vkGetPhysicalDeviceFormatProperties(Physical, VK_FORMAT_R8G8B8A8_UNORM,
                                      &Props);
  VkFormatProperties2 Props2{};
  vkGetPhysicalDeviceFormatProperties2(Physical, VK_FORMAT_R8G8B8A8_UNORM,
                                       &Props2);
  EXPECT_EQ(Props2.formatProperties.optimalTilingFeatures,
            Props.optimalTilingFeatures);
  EXPECT_EQ(Props2.formatProperties.bufferFeatures, Props.bufferFeatures);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesRejectsUnrecognizedFormat) {
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesRejectsUnsupportedUsage) {
  // (Roadmap E26) `R32_UINT` is not one of the mandatory-sampled integer
  // formats `femeRTUnpackImageTexelI32` decodes (only its four-component
  // `R32G32B32A32_UINT`/`_SINT` sibling and the packed formats are), so
  // requesting `SAMPLED_BIT` for it must still fail rather than silently
  // succeed with a descriptor that would sample as all-zero.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R32_UINT, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesAcceptsMandatoryIntegerFormat) {
  // (Roadmap E26) `R8G8B8A8_UINT` -- one of the mandatory-sampled integer
  // formats `feme.cpu.image.load.2d.v4i32` can now actually fetch -- must
  // be accepted for `SAMPLED_BIT`, unlike `R32_UINT` above.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_SUCCESS);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesRejectsStorageUsage) {
  // No format has a shader-writable storage image path yet.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, 0, &Props),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesRejectsUnsupportedCreateFlags) {
  // Only `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` is accepted at all (see
  // Image.cpp's `isValidImageShape`); sparse binding is out of scope.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_CREATE_SPARSE_BINDING_BIT, &Props),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesReportsRealLimitsFor2DSampled) {
  VkImageFormatProperties Props{};
  ASSERT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_SUCCESS);
  EXPECT_GT(Props.maxExtent.width, 1u);
  EXPECT_GT(Props.maxExtent.height, 1u);
  EXPECT_EQ(Props.maxExtent.depth, 1u);
  EXPECT_GT(Props.maxMipLevels, 1u);
  EXPECT_GT(Props.maxArrayLayers, 1u);
  EXPECT_TRUE(Props.sampleCounts & VK_SAMPLE_COUNT_1_BIT);
  EXPECT_GT(Props.maxResourceSize, VkDeviceSize(0));
}

TEST_F(EntryPointsTest, ImageFormatProperties3DReportsSingleArrayLayer) {
  VkImageFormatProperties Props{};
  ASSERT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_3D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_SUCCESS);
  // A 3D image may never have more than one array layer (Vulkan itself
  // does not allow one; see FeMeVulkanDesign.md's V5 status note).
  EXPECT_EQ(Props.maxArrayLayers, 1u);
  EXPECT_GT(Props.maxExtent.depth, 1u);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesAcceptsASTCLDRSampled) {
  // Roadmap E22/E23 wired ASTC LDR image creation and shader sampling all
  // the way through; this query must now agree, unblocking
  // `dEQP-VK.texture.*`'s own capability probe for this format.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_SUCCESS);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesRejectsASTCHDRSampled) {
  // An HDR ASTC format samples as all-zero (the RGBA8 sampling bridge is
  // LDR-only), so requesting `SAMPLED_BIT` for one must still fail.
  VkImageFormatProperties Props{};
  EXPECT_EQ(vkGetPhysicalDeviceImageFormatProperties(
                Physical, VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &Props),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

TEST_F(EntryPointsTest, ImageFormatPropertiesAcceptsASTCLDRTransferOnly) {
  // A copy never decodes (roadmap E22), so an ASTC format -- LDR or HDR --
  // is still a legal transfer-only image regardless of its sampling
  // support.
  VkImageFormatProperties Props{};
  EXPECT_EQ(
      vkGetPhysicalDeviceImageFormatProperties(
          Physical, VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT, VK_IMAGE_TYPE_2D,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0,
          &Props),
      VK_SUCCESS);
}

TEST_F(EntryPointsTest, FormatProperties2FillsChainedFormatProperties3) {
  // Roadmap E25: a chained `VkFormatProperties3` (core since Vulkan 1.3,
  // so `dEQP-VK.api.info.unsupported_image_usage.*`'s own `Context::
  // getFormatProperties` helper chains one onto every query once this
  // ICD's advertised `apiVersion` implies it's always available) used to
  // be left entirely untouched -- silently discarding every bit
  // `formatFeatureFlags` reports for any caller that chained one.
  VkFormatProperties3 Props3{};
  Props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
  VkFormatProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
  Props2.pNext = &Props3;
  vkGetPhysicalDeviceFormatProperties2(Physical, VK_FORMAT_R8G8B8A8_UNORM,
                                       &Props2);
  EXPECT_EQ(Props3.linearTilingFeatures,
            (VkFormatFeatureFlags2)Props2.formatProperties.linearTilingFeatures);
  EXPECT_EQ(
      Props3.optimalTilingFeatures,
      (VkFormatFeatureFlags2)Props2.formatProperties.optimalTilingFeatures);
  EXPECT_EQ(Props3.bufferFeatures,
            (VkFormatFeatureFlags2)Props2.formatProperties.bufferFeatures);
  EXPECT_TRUE(Props3.optimalTilingFeatures &
              VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT);
}

} // namespace
