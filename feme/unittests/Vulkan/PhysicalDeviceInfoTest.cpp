//===- PhysicalDeviceInfoTest.cpp - Truthful capability tests --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "PhysicalDeviceInfo.h"
#include "EntryPoints.h"

#include "gtest/gtest.h"

#include <cstring>
#include <limits>
#include <vector>

using namespace feme::vulkan;

namespace {

TEST(PhysicalDeviceInfo, ReportsHonestVersionAndIdentity) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();

  EXPECT_EQ(Info.Properties.apiVersion, VK_API_VERSION_1_4);

  // The Khronos "not yet assigned an official vendor ID" reserved value
  // (see "Device identity").
  EXPECT_EQ(Info.Properties.vendorID, 0x10000u);
  EXPECT_EQ(Info.DriverId, VK_DRIVER_ID_MAX_ENUM);

  // Zero `VkConformanceVersion` is the truthful value for a non-conformant
  // development ICD; the driver strings, however, must still be non-empty and
  // null-terminated once driver properties are queryable.
  EXPECT_EQ(Info.ConformanceVersion.major, 0u);
  EXPECT_EQ(Info.ConformanceVersion.minor, 0u);
  EXPECT_EQ(Info.ConformanceVersion.subminor, 0u);
  EXPECT_EQ(Info.ConformanceVersion.patch, 0u);
  EXPECT_STREQ(Info.DriverName, "FeMe Vulkan Driver");
  EXPECT_STREQ(Info.DriverInfo,
               "LLVM in-tree development ICD; no Khronos conformance claim");
  EXPECT_EQ(Info.Properties.deviceType, VK_PHYSICAL_DEVICE_TYPE_CPU);
}

TEST(PhysicalDeviceInfo, SubgroupSizeIsAPowerOfTwoInRange) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_GE(Info.SubgroupSize, 4u);
  EXPECT_LE(Info.SubgroupSize, 128u);
  EXPECT_EQ(Info.SubgroupSize & (Info.SubgroupSize - 1), 0u)
      << "subgroup size must be a power of two";
  EXPECT_EQ(Info.SubgroupSupportedStages, VK_SHADER_STAGE_COMPUTE_BIT);
  EXPECT_TRUE(Info.SubgroupSupportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT);
}

TEST(PhysicalDeviceInfo, UniversalQueueFamilyIsGraphicsComputeAndTransfer) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  const VkQueueFamilyProperties &Family = Info.QueueFamilies[0];
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_COMPUTE_BIT);
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_TRANSFER_BIT);
  // (V6) Graphics joins the one existing universal family rather than
  // adding a second one (see "Graphics queue family").
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  EXPECT_GE(Family.queueCount, 1u);
  // Timestamp queries report no valid bits: `VkQueryPool` accepts a
  // timestamp query but every value it produces is zero (see QueryPool.h),
  // which is exactly what `timestampValidBits == 0` tells an application.
  EXPECT_EQ(Family.timestampValidBits, 0u);
}

TEST(PhysicalDeviceInfo,
     DedicatedTransferQueueFamilyExcludesGraphicsAndCompute) {
  // Roadmap C7 ("Queue family capability combinations"): a second family
  // exists purely so a `TRANSFER`-only, `GRAPHICS`/`COMPUTE`-excluding
  // queue is coverable, which the universal family can never be by
  // definition.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  ASSERT_EQ(PhysicalDeviceInfo::NumQueueFamilies, 3u);
  const VkQueueFamilyProperties &Family = Info.QueueFamilies[1];
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_TRANSFER_BIT);
  EXPECT_FALSE(Family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  EXPECT_FALSE(Family.queueFlags & VK_QUEUE_COMPUTE_BIT);
  EXPECT_GE(Family.queueCount, 1u);
}

TEST(PhysicalDeviceInfo, DedicatedComputeQueueFamilyExcludesGraphics) {
  // Roadmap C7: a third family covers the mandatory CTS combination that
  // needs `COMPUTE` while excluding `GRAPHICS` (e.g.
  // `dEQP-VK.api.buffer_marker.compute.*`).
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  ASSERT_EQ(PhysicalDeviceInfo::NumQueueFamilies, 3u);
  const VkQueueFamilyProperties &Family = Info.QueueFamilies[2];
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_COMPUTE_BIT);
  EXPECT_FALSE(Family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  EXPECT_GE(Family.queueCount, 1u);
}

TEST(PhysicalDeviceInfo, MemoryHeapReflectsRealHostMemory) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  ASSERT_GE(Info.MemoryProperties.memoryHeapCount, 1u);
  EXPECT_GT(Info.MemoryProperties.memoryHeaps[0].size, 0u);
  ASSERT_GE(Info.MemoryProperties.memoryTypeCount, 1u);
  VkMemoryPropertyFlags Flags =
      Info.MemoryProperties.memoryTypes[0].propertyFlags;
  EXPECT_TRUE(Flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  EXPECT_TRUE(Flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

TEST(PhysicalDeviceInfo,
     OnlyRobustBufferAccessDualSrcBlendAndASTCLDRAreAdvertised) {
  // (V4/C4/E22) `robustBufferAccess`/`dualSrcBlend`/
  // `textureCompressionASTC_LDR` are the only core features this
  // milestone can honestly claim (see PhysicalDeviceInfo.cpp's comment);
  // every other `VkBool32` stays false, since nothing else has been
  // implemented that could back one yet.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_EQ(Info.Features.robustBufferAccess, VK_TRUE);
  EXPECT_EQ(Info.Features.dualSrcBlend, VK_TRUE);
  EXPECT_EQ(Info.Features.textureCompressionASTC_LDR, VK_TRUE);

  VkPhysicalDeviceFeatures Cleared = Info.Features;
  Cleared.robustBufferAccess = VK_FALSE;
  Cleared.dualSrcBlend = VK_FALSE;
  Cleared.textureCompressionASTC_LDR = VK_FALSE;
  VkPhysicalDeviceFeatures Zero{};
  EXPECT_EQ(std::memcmp(&Cleared, &Zero, sizeof(Zero)), 0);
}

TEST(PhysicalDeviceInfo, TextureCompressionASTCLDRIsAdvertised) {
  // Roadmap E22: `vkCreateImage` now accepts a block-compressed
  // `VkFormat`, and `ImageOps.cpp`'s `runBlitImage` decodes an LDR ASTC
  // source through `ASTCDecode.h`'s `decodeASTCBlock` -- this Vulkan 1.0
  // core feature bit (tracked explicitly since roadmap E20, the same way
  // `textureCompressionASTC_HDR` is tracked in EntryPoints.cpp's
  // aggregate feature struct case) can now honestly flip to `VK_TRUE`.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_EQ(Info.Features.textureCompressionASTC_LDR, VK_TRUE);
}

TEST(PhysicalDeviceInfo, DeviceAndPipelineCacheUUIDsDiffer) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_NE(std::memcmp(Info.DeviceUUID, Info.Properties.pipelineCacheUUID,
                        VK_UUID_SIZE),
            0);
}

class PhysicalDeviceProperties2Test : public ::testing::Test {
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

TEST_F(PhysicalDeviceProperties2Test,
       SubgroupBasicBitMatchesPromotedVulkan11Properties) {
  VkPhysicalDeviceSubgroupProperties Subgroup{};
  Subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
  VkPhysicalDeviceVulkan11Properties Props11{};
  Props11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
  Subgroup.pNext = &Props11;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Subgroup;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  EXPECT_TRUE(Subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT);
  EXPECT_TRUE(Props11.subgroupSupportedOperations &
              VK_SUBGROUP_FEATURE_BASIC_BIT);
  EXPECT_EQ(Subgroup.subgroupSize, Props11.subgroupSize);
  EXPECT_EQ(Subgroup.supportedStages, Props11.subgroupSupportedStages);
}

TEST_F(PhysicalDeviceProperties2Test,
       DriverPropertiesReportTruthfulStringsAndZeroConformance) {
  VkPhysicalDeviceDriverProperties DriverProps{};
  DriverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
  VkPhysicalDeviceVulkan12Properties Props12{};
  Props12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
  DriverProps.pNext = &Props12;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &DriverProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  EXPECT_EQ(DriverProps.driverID, VK_DRIVER_ID_MAX_ENUM);
  EXPECT_EQ(Props12.driverID, DriverProps.driverID);
  EXPECT_NE(std::strlen(DriverProps.driverName), 0u);
  EXPECT_NE(std::strlen(DriverProps.driverInfo), 0u);
  EXPECT_NE(
      std::memchr(DriverProps.driverName, '\0', sizeof(DriverProps.driverName)),
      nullptr);
  EXPECT_NE(
      std::memchr(DriverProps.driverInfo, '\0', sizeof(DriverProps.driverInfo)),
      nullptr);
  EXPECT_STREQ(Props12.driverName, DriverProps.driverName);
  EXPECT_STREQ(Props12.driverInfo, DriverProps.driverInfo);
  EXPECT_EQ(DriverProps.conformanceVersion.major, 0u);
  EXPECT_EQ(DriverProps.conformanceVersion.minor, 0u);
  EXPECT_EQ(DriverProps.conformanceVersion.subminor, 0u);
  EXPECT_EQ(DriverProps.conformanceVersion.patch, 0u);
  EXPECT_EQ(Props12.conformanceVersion.major,
            DriverProps.conformanceVersion.major);
  EXPECT_EQ(Props12.conformanceVersion.minor,
            DriverProps.conformanceVersion.minor);
  EXPECT_EQ(Props12.conformanceVersion.subminor,
            DriverProps.conformanceVersion.subminor);
  EXPECT_EQ(Props12.conformanceVersion.patch,
            DriverProps.conformanceVersion.patch);
}

TEST(PhysicalDeviceInfo, IsDeterministic) {
  PhysicalDeviceInfo A = computePhysicalDeviceInfo();
  PhysicalDeviceInfo B = computePhysicalDeviceInfo();
  EXPECT_EQ(A.SubgroupSize, B.SubgroupSize);
  EXPECT_EQ(std::memcmp(A.DeviceUUID, B.DeviceUUID, VK_UUID_SIZE), 0);
  EXPECT_EQ(std::memcmp(A.Properties.pipelineCacheUUID,
                        B.Properties.pipelineCacheUUID, VK_UUID_SIZE),
            0);
}

TEST(PhysicalDeviceInfo, ReportsMandatory1p2LimitsAtOrAboveTheRequiredMinimum) {
  // Roadmap C6: `dEQP-VK.api.info.vulkan1p2_limits_validation` checks
  // these unconditionally once the advertised API version is >= 1.2, even
  // though the features they are nominally attached to
  // (`multiview`/`timelineSemaphore`) may not themselves be advertised.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_GE(Info.MaxMemoryAllocationSize, VkDeviceSize{1} << 30);
  EXPECT_GE(Info.MaxPerSetDescriptors, 1024u);
  EXPECT_GE(Info.MaxMultiviewViewCount, 6u);
  EXPECT_GE(Info.MaxMultiviewInstanceIndex, (1u << 27) - 1);
  // A timeline semaphore's counter is a plain `uint64_t` compare (see
  // `feme::vulkan::Semaphore`), so the honest value is the type's own
  // maximum, not merely the spec's `2^31-1` floor.
  EXPECT_EQ(Info.MaxTimelineSemaphoreValueDifference,
            std::numeric_limits<uint64_t>::max());
}

TEST_F(PhysicalDeviceProperties2Test,
       MultiviewAndMaintenance3PropertiesMatchPromotedVulkan11Properties) {
  VkPhysicalDeviceMultiviewProperties Multiview{};
  Multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
  VkPhysicalDeviceMaintenance3Properties Maintenance3{};
  Maintenance3.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
  Multiview.pNext = &Maintenance3;
  VkPhysicalDeviceVulkan11Properties Props11{};
  Props11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
  Props11.pNext = &Multiview;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Props11;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  EXPECT_EQ(Multiview.maxMultiviewViewCount, Props11.maxMultiviewViewCount);
  EXPECT_EQ(Multiview.maxMultiviewInstanceIndex,
            Props11.maxMultiviewInstanceIndex);
  EXPECT_EQ(Maintenance3.maxPerSetDescriptors, Props11.maxPerSetDescriptors);
  EXPECT_EQ(Maintenance3.maxMemoryAllocationSize,
            Props11.maxMemoryAllocationSize);
  EXPECT_GE(Props11.maxMultiviewViewCount, 6u);
  EXPECT_GE(Props11.maxMemoryAllocationSize, VkDeviceSize{1} << 30);
}

TEST_F(PhysicalDeviceProperties2Test,
       TimelineSemaphorePropertiesMatchPromotedVulkan12Properties) {
  VkPhysicalDeviceTimelineSemaphoreProperties TimelineSemaphore{};
  TimelineSemaphore.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES;
  VkPhysicalDeviceVulkan12Properties Props12{};
  Props12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
  TimelineSemaphore.pNext = &Props12;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &TimelineSemaphore;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  EXPECT_EQ(TimelineSemaphore.maxTimelineSemaphoreValueDifference,
            Props12.maxTimelineSemaphoreValueDifference);
  EXPECT_EQ(Props12.maxTimelineSemaphoreValueDifference,
            std::numeric_limits<uint64_t>::max());
}

TEST_F(PhysicalDeviceProperties2Test,
       Vulkan13PropertiesEnumerateEveryMandatoryLimitConservatively) {
  // Roadmap E2: every one of the aggregate `VkPhysicalDeviceVulkan13
  // Properties` struct's 46 limit fields must be explicitly written, for
  // the same unwritten-field guard reason
  // `MultiviewFeaturesReportMultiviewTrueAmplificationFalse` below uses (a
  // 0xAA fill pattern would otherwise leave an unset field looking like a
  // plausible, but coincidental, non-zero value). Every field but
  // `maxBufferSize` (roadmap E4), the four `subgroupSizeControl` fields
  // (roadmap E7), and the six `inlineUniformBlock` fields (roadmap E14,
  // all real once their own extension landed, see below) is
  // `0`/`VK_FALSE`: each one is cross-checked by
  // `dEQP-VK.api.info.vulkan1p3.property_extensions_consistency` against
  // its own still-unimplemented dedicated-extension struct (see
  // EntryPoints.cpp's case comment), so a real, nonzero value here would
  // regress that currently-passing case rather than close one.
  VkPhysicalDeviceVulkan13Properties Props13;
  std::memset(&Props13, 0xAA, sizeof(Props13));
  Props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
  Props13.pNext = nullptr;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Props13;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  // (roadmap E7) Real once `VK_EXT_subgroup_size_control` landed: see
  // `SubgroupSizeControlPropertiesMatchDedicatedStruct` below for the
  // dedicated-struct cross-check these must agree with.
  EXPECT_EQ(Props13.minSubgroupSize, 4u);
  EXPECT_EQ(Props13.maxSubgroupSize, 128u);
  EXPECT_EQ(Props13.maxComputeWorkgroupSubgroups, 32u);
  EXPECT_EQ(Props13.requiredSubgroupSizeStages,
            static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
  // (roadmap E14) Real once `VK_EXT_inline_uniform_block` landed: see
  // `InlineUniformBlockPropertiesMatchDedicatedStruct` below for the
  // dedicated-struct cross-check these must agree with. The two
  // `UpdateAfterBind` variants equal their non-`UpdateAfterBind`
  // counterparts, not `0`, even though
  // `descriptorBindingInlineUniformBlockUpdateAfterBind` stays `VK_FALSE`
  // (no update-after-bind/descriptor-indexing mechanism exists in this
  // ICD at all yet): per spec both are required limits independent of
  // that feature bit -- found by a targeted CTS run of
  // `dEQP-VK.api.info.vulkan1p2_limits_validation.ext_inline_uniform_block`,
  // which enforces the same `>= 4` floor on them unconditionally once
  // this extension is advertised.
  EXPECT_EQ(Props13.maxInlineUniformBlockSize, 256u);
  EXPECT_EQ(Props13.maxPerStageDescriptorInlineUniformBlocks, 4u);
  EXPECT_EQ(Props13.maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks,
            4u);
  EXPECT_EQ(Props13.maxDescriptorSetInlineUniformBlocks, 4u);
  EXPECT_EQ(Props13.maxDescriptorSetUpdateAfterBindInlineUniformBlocks, 4u);
  EXPECT_EQ(Props13.maxInlineUniformTotalSize, 1024u);
  // (roadmap E8) All 36 `integerDotProduct*Accelerated` bits stay
  // `VK_FALSE`: a real `spirv`->`llvm` lowering exists
  // (SPIRVToLLVMPatterns.cpp), but it is an ordinary CPU multiply-add
  // sequence, not a hardware-accelerated one.
  EXPECT_EQ(Props13.integerDotProduct8BitUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct8BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct8BitMixedSignednessAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct4x8BitPackedUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct4x8BitPackedSignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct4x8BitPackedMixedSignednessAccelerated,
            VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct16BitUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct16BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct16BitMixedSignednessAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct32BitUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct32BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct32BitMixedSignednessAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct64BitUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct64BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(Props13.integerDotProduct64BitMixedSignednessAccelerated, VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating8BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating8BitSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating16BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating16BitSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating32BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating32BitSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating64BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13.integerDotProductAccumulatingSaturating64BitSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      Props13
          .integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated,
      VK_FALSE);
  // Roadmap E18: real once `VK_EXT_texel_buffer_alignment` landed --
  // `vkCreateBufferView` never enforces an offset alignment stricter than
  // the core 1.0 `minTexelBufferOffsetAlignment` limit, and a
  // single-texel-sized offset is always sufficient too. Must agree with
  // the dedicated `VkPhysicalDeviceTexelBufferAlignmentProperties` case
  // below.
  EXPECT_EQ(Props13.storageTexelBufferOffsetAlignmentBytes, 256u);
  EXPECT_EQ(Props13.storageTexelBufferOffsetSingleTexelAlignment, VK_TRUE);
  EXPECT_EQ(Props13.uniformTexelBufferOffsetAlignmentBytes, 256u);
  EXPECT_EQ(Props13.uniformTexelBufferOffsetSingleTexelAlignment, VK_TRUE);
  // (roadmap E4) Real once `VK_KHR_maintenance4` landed: agrees with
  // `VkPhysicalDeviceMaintenance3Properties::maxMemoryAllocationSize`
  // (there is no further, buffer-specific limit beyond the host memory
  // size both report).
  EXPECT_GE(Props13.maxBufferSize, VkDeviceSize{1} << 30);
}

TEST_F(PhysicalDeviceProperties2Test,
       Vulkan14PropertiesEnumerateEveryMandatoryLimitConservatively) {
  // Roadmap E2: every one of the aggregate `VkPhysicalDeviceVulkan14
  // Properties` struct's 25 limit fields must be explicitly written,
  // guarded the same way the 1.3 test above guards its own struct, and for
  // the same "stay in sync with each still-unimplemented dedicated
  // extension struct" reason.
  VkPhysicalDeviceVulkan14Properties Props14;
  std::memset(&Props14, 0xAA, sizeof(Props14));
  Props14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
  Props14.pNext = nullptr;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Props14;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  // (roadmap F5) Real once `VK_KHR_line_rasterization` landed: agrees
  // with `VkPhysicalDeviceLineRasterizationPropertiesKHR::
  // lineSubPixelPrecisionBits`'s own dedicated-struct test below.
  EXPECT_EQ(Props14.lineSubPixelPrecisionBits, 4u);
  // (roadmap F6) Real once `VK_KHR_vertex_attribute_divisor` landed: agrees
  // with `VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR`'s own
  // dedicated-struct test below.
  EXPECT_EQ(Props14.maxVertexAttribDivisor, 0xFFFFFFFFu);
  EXPECT_EQ(Props14.supportsNonZeroFirstInstance, VK_TRUE);
  // (roadmap F12) Real once `VK_KHR_push_descriptor` landed: agrees with
  // `VkPhysicalDevicePushDescriptorProperties`'s own dedicated-struct test
  // below.
  EXPECT_EQ(Props14.maxPushDescriptors, 32u);
  // (roadmap F8c) `dynamicRenderingLocalRead` now covers a depth/stencil
  // attachment (F8b) and a multisample one (F8c, agrees with the
  // dedicated-struct feature bit's own comment) -- both flip to `VK_TRUE`.
  EXPECT_EQ(Props14.dynamicRenderingLocalReadDepthStencilAttachments, VK_TRUE);
  EXPECT_EQ(Props14.dynamicRenderingLocalReadMultisampledAttachments, VK_TRUE);
  EXPECT_EQ(Props14.earlyFragmentMultisampleCoverageAfterSampleCounting,
            VK_FALSE);
  EXPECT_EQ(Props14.earlyFragmentSampleMaskTestBeforeSampleCounting, VK_FALSE);
  EXPECT_EQ(Props14.depthStencilSwizzleOneSupport, VK_FALSE);
  EXPECT_EQ(Props14.polygonModePointSize, VK_FALSE);
  EXPECT_EQ(Props14.nonStrictSinglePixelWideLinesUseParallelogram, VK_FALSE);
  EXPECT_EQ(Props14.nonStrictWideLinesUseParallelogram, VK_FALSE);
  EXPECT_EQ(Props14.blockTexelViewCompatibleMultipleLayers, VK_FALSE);
  // Roadmap E6: a real value -- with no multi-planar/YCbCr sampler support,
  // a combined image sampler descriptor always consumes exactly one
  // descriptor slot.
  EXPECT_EQ(Props14.maxCombinedImageSamplerDescriptorCount, 1u);
  EXPECT_EQ(Props14.fragmentShadingRateClampCombinerInputs, VK_FALSE);
  // (roadmap F10) Real once `VK_EXT_pipeline_robustness` landed: agrees
  // with `VkPhysicalDevicePipelineRobustnessProperties`'s own
  // dedicated-struct test below.
  EXPECT_EQ(Props14.defaultRobustnessStorageBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(Props14.defaultRobustnessUniformBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(Props14.defaultRobustnessVertexInputs,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(Props14.defaultRobustnessImages,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS);
  // (roadmap F11) Real once `VK_EXT_host_image_copy` landed: agrees with
  // `VkPhysicalDeviceHostImageCopyProperties`'s own dedicated-struct test
  // below.
  EXPECT_EQ(Props14.copySrcLayoutCount, 2u);
  ASSERT_NE(Props14.pCopySrcLayouts, nullptr);
  EXPECT_EQ(Props14.pCopySrcLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(Props14.pCopySrcLayouts[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  EXPECT_EQ(Props14.copyDstLayoutCount, 2u);
  ASSERT_NE(Props14.pCopyDstLayouts, nullptr);
  EXPECT_EQ(Props14.pCopyDstLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(Props14.pCopyDstLayouts[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  uint8_t ZeroUUID[VK_UUID_SIZE] = {};
  EXPECT_NE(
      std::memcmp(Props14.optimalTilingLayoutUUID, ZeroUUID, VK_UUID_SIZE), 0);
  EXPECT_EQ(Props14.identicalMemoryTypeRequirements, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       Roadmap6FeaturesAreAdvertisedThroughDedicatedAndVulkan12Chains) {
  // Roadmap C6: each of these is truthfully implemented (see
  // EntryPoints.cpp's `fillFeatures2Chain` case comments), so both the
  // dedicated feature struct and the aggregate `VkPhysicalDeviceVulkan12
  // Features` must agree once chained.
  VkPhysicalDeviceHostQueryResetFeatures HostQueryReset{};
  HostQueryReset.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
  VkPhysicalDeviceUniformBufferStandardLayoutFeatures UniformBufferLayout{};
  UniformBufferLayout.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
  HostQueryReset.pNext = &UniformBufferLayout;
  VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures SeparateDSLayouts{};
  SeparateDSLayouts.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES;
  UniformBufferLayout.pNext = &SeparateDSLayouts;
  VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures ExtendedTypes{};
  ExtendedTypes.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES;
  SeparateDSLayouts.pNext = &ExtendedTypes;
  VkPhysicalDeviceVulkan12Features Features12{};
  Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  ExtendedTypes.pNext = &Features12;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &HostQueryReset;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  EXPECT_EQ(HostQueryReset.hostQueryReset, VK_TRUE);
  EXPECT_EQ(UniformBufferLayout.uniformBufferStandardLayout, VK_TRUE);
  EXPECT_EQ(SeparateDSLayouts.separateDepthStencilLayouts, VK_TRUE);
  EXPECT_EQ(ExtendedTypes.shaderSubgroupExtendedTypes, VK_TRUE);
  EXPECT_EQ(Features12.hostQueryReset, HostQueryReset.hostQueryReset);
  EXPECT_EQ(Features12.uniformBufferStandardLayout,
            UniformBufferLayout.uniformBufferStandardLayout);
  EXPECT_EQ(Features12.separateDepthStencilLayouts,
            SeparateDSLayouts.separateDepthStencilLayouts);
  EXPECT_EQ(Features12.shaderSubgroupExtendedTypes,
            ExtendedTypes.shaderSubgroupExtendedTypes);
  EXPECT_EQ(Features12.subgroupBroadcastDynamicId, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       DynamicRenderingIsAdvertisedThroughAggregateVulkan13Features) {
  // Roadmap E1: `VkPhysicalDeviceVulkan13Features.dynamicRendering` must
  // agree with the dedicated `VK_KHR_dynamic_rendering` struct case above
  // it -- pre-filled with a non-zero pattern first (the same
  // unwritten-field guard `MultiviewFeaturesReportMultiviewTrueAmplificationFalse`
  // below uses) so every other 1.3 bit's explicit `VK_FALSE` is verified
  // rather than merely a pre-existing zero.
  VkPhysicalDeviceVulkan13Features Features13;
  std::memset(&Features13, 0xAA, sizeof(Features13));
  Features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  Features13.pNext = nullptr;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Features13;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  EXPECT_EQ(Features13.dynamicRendering, VK_TRUE);
  EXPECT_EQ(Features13.robustImageAccess, VK_FALSE);
  // Roadmap E14: now genuinely implemented (Descriptor.{h,cpp}'s byte-blob
  // descriptor storage), and must agree with the dedicated
  // `VK_EXT_inline_uniform_block` struct case below.
  // `descriptorBindingInlineUniformBlockUpdateAfterBind` stays `VK_FALSE`:
  // no update-after-bind/descriptor-indexing mechanism exists in this ICD
  // at all yet.
  EXPECT_EQ(Features13.inlineUniformBlock, VK_TRUE);
  EXPECT_EQ(Features13.descriptorBindingInlineUniformBlockUpdateAfterBind,
            VK_FALSE);
  // Roadmap E9: now genuinely implemented (Pipeline.cpp/GraphicsPipeline.cpp
  // honor VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT,
  // PipelineCache.{h,cpp} honors VK_PIPELINE_CACHE_CREATE_EXTERNALLY_
  // SYNCHRONIZED_BIT), and must agree with the dedicated
  // `VK_EXT_pipeline_creation_cache_control` struct case below.
  EXPECT_EQ(Features13.pipelineCreationCacheControl, VK_TRUE);
  // Roadmap E10: now genuinely implemented (PrivateData.{h,cpp}), and must
  // agree with the dedicated `VK_EXT_private_data` struct case below.
  EXPECT_EQ(Features13.privateData, VK_TRUE);
  // Roadmap E11: now genuinely implemented (SPIRVToLLVMPatterns.cpp/
  // CanonicalizeStage.cpp convert OpDemoteToHelperInvocation to
  // feme.stage.demote), and must agree with the dedicated
  // `VK_EXT_shader_demote_to_helper_invocation` struct case below.
  EXPECT_EQ(Features13.shaderDemoteToHelperInvocation, VK_TRUE);
  // Roadmap E12: now genuinely implemented (SPIRVToLLVMPatterns.cpp
  // converts OpTerminateInvocation to an unconditional discard-and-return),
  // and must agree with the dedicated `VK_KHR_shader_terminate_invocation`
  // struct case below.
  EXPECT_EQ(Features13.shaderTerminateInvocation, VK_TRUE);
  // Roadmap E7: now genuinely implemented (Pipeline.cpp honors a chained
  // `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` and
  // `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`), and must
  // agree with the dedicated `VK_EXT_subgroup_size_control` struct case
  // below.
  EXPECT_EQ(Features13.subgroupSizeControl, VK_TRUE);
  EXPECT_EQ(Features13.computeFullSubgroups, VK_TRUE);
  // Roadmap E3: now genuinely implemented (CommandBuffer.cpp/Sync.cpp),
  // and must agree with the dedicated `VK_KHR_synchronization2` struct
  // case below.
  EXPECT_EQ(Features13.synchronization2, VK_TRUE);
  EXPECT_EQ(Features13.textureCompressionASTC_HDR, VK_FALSE);
  // Roadmap E13: now genuinely implemented
  // (WorkgroupGlobalVariablePattern/GroupSharedLayout::NeedsZeroInit), and
  // must agree with the dedicated
  // `VK_KHR_zero_initialize_workgroup_memory` struct case below.
  EXPECT_EQ(Features13.shaderZeroInitializeWorkgroupMemory, VK_TRUE);
  // Roadmap E8: now genuinely implemented (SPIRVToLLVMPatterns.cpp's
  // OpSDot/OpUDot/OpSUDot-family patterns), and must agree with the
  // dedicated `VK_KHR_shader_integer_dot_product` struct case below. This
  // does not raise any of the 36 `integerDotProduct*Accelerated` limit
  // bits (`Vulkan13PropertiesEnumerateEveryMandatoryLimitConservatively`
  // above): a real lowering exists, but it is an ordinary CPU multiply-add
  // sequence, not a hardware-accelerated one.
  EXPECT_EQ(Features13.shaderIntegerDotProduct, VK_TRUE);
  // Roadmap E4: now genuinely implemented (Buffer.cpp/Image.cpp), and must
  // agree with the dedicated `VK_KHR_maintenance4` struct case below.
  EXPECT_EQ(Features13.maintenance4, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    SubgroupSizeControlIsAdvertisedThroughItsOwnDedicatedFeatureAndPropertyStructs) {
  // Roadmap E7: `VK_EXT_subgroup_size_control`'s own dedicated
  // feature/properties structs must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features`/`Properties` cases above, exactly
  // like `VK_KHR_maintenance4`'s own structs do.
  VkPhysicalDeviceSubgroupSizeControlFeatures SubgroupSizeControlFeatures{};
  SubgroupSizeControlFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &SubgroupSizeControlFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(SubgroupSizeControlFeatures.subgroupSizeControl, VK_TRUE);
  EXPECT_EQ(SubgroupSizeControlFeatures.computeFullSubgroups, VK_TRUE);

  VkPhysicalDeviceSubgroupSizeControlProperties SubgroupSizeControlProps{};
  SubgroupSizeControlProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &SubgroupSizeControlProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(SubgroupSizeControlProps.minSubgroupSize, 4u);
  EXPECT_EQ(SubgroupSizeControlProps.maxSubgroupSize, 128u);
  EXPECT_EQ(SubgroupSizeControlProps.maxComputeWorkgroupSubgroups, 32u);
  EXPECT_EQ(SubgroupSizeControlProps.requiredSubgroupSizeStages,
            static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));

  VkPhysicalDeviceVulkan13Properties Props13{};
  Props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
  Props2.pNext = &Props13;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(SubgroupSizeControlProps.minSubgroupSize, Props13.minSubgroupSize);
  EXPECT_EQ(SubgroupSizeControlProps.maxSubgroupSize, Props13.maxSubgroupSize);
  EXPECT_EQ(SubgroupSizeControlProps.maxComputeWorkgroupSubgroups,
            Props13.maxComputeWorkgroupSubgroups);
  EXPECT_EQ(SubgroupSizeControlProps.requiredSubgroupSizeStages,
            Props13.requiredSubgroupSizeStages);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    InlineUniformBlockIsAdvertisedThroughItsOwnDedicatedFeatureAndPropertyStructs) {
  // Roadmap E14: `VK_EXT_inline_uniform_block`'s own dedicated
  // feature/properties structs must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features`/`Properties` cases above, exactly
  // like `VK_EXT_subgroup_size_control`'s own structs do.
  VkPhysicalDeviceInlineUniformBlockFeatures InlineUniformBlockFeatures{};
  InlineUniformBlockFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &InlineUniformBlockFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(InlineUniformBlockFeatures.inlineUniformBlock, VK_TRUE);
  EXPECT_EQ(InlineUniformBlockFeatures
                .descriptorBindingInlineUniformBlockUpdateAfterBind,
            VK_FALSE);

  VkPhysicalDeviceInlineUniformBlockProperties InlineUniformBlockProps{};
  InlineUniformBlockProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &InlineUniformBlockProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(InlineUniformBlockProps.maxInlineUniformBlockSize, 256u);
  EXPECT_EQ(InlineUniformBlockProps.maxPerStageDescriptorInlineUniformBlocks,
            4u);
  // Equal to the non-`UpdateAfterBind` field above, not `0`: per spec
  // these two are required limits independent of
  // `descriptorBindingInlineUniformBlockUpdateAfterBind` (see the
  // aggregate-struct test above).
  EXPECT_EQ(InlineUniformBlockProps
                .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks,
            4u);
  EXPECT_EQ(InlineUniformBlockProps.maxDescriptorSetInlineUniformBlocks, 4u);
  EXPECT_EQ(InlineUniformBlockProps
                .maxDescriptorSetUpdateAfterBindInlineUniformBlocks,
            4u);

  VkPhysicalDeviceVulkan13Properties Props13{};
  Props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
  Props2.pNext = &Props13;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(InlineUniformBlockProps.maxInlineUniformBlockSize,
            Props13.maxInlineUniformBlockSize);
  EXPECT_EQ(InlineUniformBlockProps.maxPerStageDescriptorInlineUniformBlocks,
            Props13.maxPerStageDescriptorInlineUniformBlocks);
  EXPECT_EQ(InlineUniformBlockProps.maxDescriptorSetInlineUniformBlocks,
            Props13.maxDescriptorSetInlineUniformBlocks);
}

TEST_F(PhysicalDeviceProperties2Test,
       TexelBufferAlignmentIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E18: `VK_EXT_texel_buffer_alignment`'s own dedicated feature
  // struct is unconditionally `VK_TRUE` -- unlike every other row's
  // feature bit, this extension has no aggregate 1.3/1.4 feature-struct
  // field to agree with, since only its *properties* struct was promoted
  // to core 1.3.
  VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT
      TexelBufferAlignmentFeatures{};
  TexelBufferAlignmentFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &TexelBufferAlignmentFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(TexelBufferAlignmentFeatures.texelBufferAlignment, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       TexelBufferAlignmentIsAdvertisedThroughItsOwnDedicatedPropertyStruct) {
  // Roadmap E18: `VK_EXT_texel_buffer_alignment`'s own dedicated
  // properties struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Properties` case above, exactly like
  // `VK_EXT_inline_uniform_block`'s own struct does.
  VkPhysicalDeviceTexelBufferAlignmentProperties TexelBufferAlignmentProps{};
  TexelBufferAlignmentProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &TexelBufferAlignmentProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(TexelBufferAlignmentProps.storageTexelBufferOffsetAlignmentBytes,
            256u);
  EXPECT_EQ(
      TexelBufferAlignmentProps.storageTexelBufferOffsetSingleTexelAlignment,
      VK_TRUE);
  EXPECT_EQ(TexelBufferAlignmentProps.uniformTexelBufferOffsetAlignmentBytes,
            256u);
  EXPECT_EQ(
      TexelBufferAlignmentProps.uniformTexelBufferOffsetSingleTexelAlignment,
      VK_TRUE);

  VkPhysicalDeviceVulkan13Properties Props13{};
  Props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
  Props2.pNext = &Props13;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(TexelBufferAlignmentProps.storageTexelBufferOffsetAlignmentBytes,
            Props13.storageTexelBufferOffsetAlignmentBytes);
  EXPECT_EQ(
      TexelBufferAlignmentProps.storageTexelBufferOffsetSingleTexelAlignment,
      Props13.storageTexelBufferOffsetSingleTexelAlignment);
  EXPECT_EQ(TexelBufferAlignmentProps.uniformTexelBufferOffsetAlignmentBytes,
            Props13.uniformTexelBufferOffsetAlignmentBytes);
  EXPECT_EQ(
      TexelBufferAlignmentProps.uniformTexelBufferOffsetSingleTexelAlignment,
      Props13.uniformTexelBufferOffsetSingleTexelAlignment);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    PipelineCreationCacheControlIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E9: `VK_EXT_pipeline_creation_cache_control`'s own dedicated
  // feature struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features` case above, exactly like
  // `VK_KHR_maintenance4`'s own struct does.
  VkPhysicalDevicePipelineCreationCacheControlFeatures CacheControlFeatures{};
  CacheControlFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &CacheControlFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(CacheControlFeatures.pipelineCreationCacheControl, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       PrivateDataIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E10: `VK_EXT_private_data`'s own dedicated feature struct must
  // agree with the aggregate `VkPhysicalDeviceVulkan13Features` case above,
  // exactly like `VK_EXT_pipeline_creation_cache_control`'s own struct does.
  VkPhysicalDevicePrivateDataFeatures PrivateDataFeatures{};
  PrivateDataFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &PrivateDataFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(PrivateDataFeatures.privateData, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    ShaderDemoteToHelperInvocationIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E11: `VK_EXT_shader_demote_to_helper_invocation`'s own dedicated
  // feature struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features` case above, exactly like
  // `VK_EXT_private_data`'s own struct does.
  VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures DemoteFeatures{};
  DemoteFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &DemoteFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(DemoteFeatures.shaderDemoteToHelperInvocation, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    ShaderTerminateInvocationIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E12: `VK_KHR_shader_terminate_invocation`'s own dedicated
  // feature struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features` case above, exactly like
  // `VK_EXT_shader_demote_to_helper_invocation`'s own struct does.
  VkPhysicalDeviceShaderTerminateInvocationFeatures TerminateFeatures{};
  TerminateFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &TerminateFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(TerminateFeatures.shaderTerminateInvocation, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    ShaderZeroInitializeWorkgroupMemoryIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E13: `VK_KHR_zero_initialize_workgroup_memory`'s own dedicated
  // feature struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features` case above, exactly like
  // `VK_KHR_shader_terminate_invocation`'s own struct does.
  VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures ZeroInitFeatures{};
  ZeroInitFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &ZeroInitFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(ZeroInitFeatures.shaderZeroInitializeWorkgroupMemory, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       Synchronization2IsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E3: `VK_KHR_synchronization2`'s own dedicated feature struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan13Features` case
  // above, exactly like `VK_KHR_dynamic_rendering`'s own struct does for
  // `dynamicRendering`.
  VkPhysicalDeviceSynchronization2Features Sync2{};
  Sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Sync2;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  EXPECT_EQ(Sync2.synchronization2, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    Maintenance4IsAdvertisedThroughItsOwnDedicatedFeatureAndPropertyStructs) {
  // Roadmap E4: `VK_KHR_maintenance4`'s own dedicated feature/properties
  // structs must agree with the aggregate `VkPhysicalDeviceVulkan13
  // Features`/`Properties` cases above, exactly like `VK_KHR_
  // synchronization2`'s own structs do above.
  VkPhysicalDeviceMaintenance4Features Maintenance4Features{};
  Maintenance4Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Maintenance4Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Maintenance4Features.maintenance4, VK_TRUE);

  VkPhysicalDeviceMaintenance4Properties Maintenance4Props{};
  Maintenance4Props.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Maintenance4Props;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_GE(Maintenance4Props.maxBufferSize, VkDeviceSize{1} << 30);

  VkPhysicalDeviceVulkan13Properties Props13{};
  Props13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
  Props2.pNext = &Props13;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(Maintenance4Props.maxBufferSize, Props13.maxBufferSize);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    ShaderIntegerDotProductIsAdvertisedThroughItsOwnDedicatedFeatureAndPropertyStructs) {
  // Roadmap E8: `VK_KHR_shader_integer_dot_product`'s own dedicated
  // feature/properties structs must agree with the aggregate
  // `VkPhysicalDeviceVulkan13Features`/`Properties` cases above, exactly
  // like `VK_KHR_maintenance4`'s own structs do above. The feature bit is
  // genuinely `VK_TRUE` (a real `spirv`->`llvm` lowering exists), but
  // every `integerDotProduct*Accelerated` limit stays `VK_FALSE`: this CPU
  // target executes the lowering as an ordinary multiply-add sequence,
  // not a hardware-accelerated one, and a truthful "supported but not
  // accelerated" answer is fully conformant.
  VkPhysicalDeviceShaderIntegerDotProductFeatures DotProductFeatures{};
  DotProductFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &DotProductFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(DotProductFeatures.shaderIntegerDotProduct, VK_TRUE);

  VkPhysicalDeviceShaderIntegerDotProductProperties DotProductProps;
  std::memset(&DotProductProps, 0xAA, sizeof(DotProductProps));
  DotProductProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES;
  DotProductProps.pNext = nullptr;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &DotProductProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(DotProductProps.integerDotProduct8BitUnsignedAccelerated, VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct8BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct8BitMixedSignednessAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct4x8BitPackedUnsignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct4x8BitPackedSignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(
      DotProductProps.integerDotProduct4x8BitPackedMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct16BitUnsignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct16BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct16BitMixedSignednessAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct32BitUnsignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct32BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct32BitMixedSignednessAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct64BitUnsignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct64BitSignedAccelerated, VK_FALSE);
  EXPECT_EQ(DotProductProps.integerDotProduct64BitMixedSignednessAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps
                .integerDotProductAccumulatingSaturating8BitUnsignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(DotProductProps
                .integerDotProductAccumulatingSaturating8BitSignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating16BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(DotProductProps
                .integerDotProductAccumulatingSaturating16BitSignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating32BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(DotProductProps
                .integerDotProductAccumulatingSaturating32BitSignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated,
      VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating64BitUnsignedAccelerated,
      VK_FALSE);
  EXPECT_EQ(DotProductProps
                .integerDotProductAccumulatingSaturating64BitSignedAccelerated,
            VK_FALSE);
  EXPECT_EQ(
      DotProductProps
          .integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated,
      VK_FALSE);
}

TEST_F(PhysicalDeviceProperties2Test,
       Maintenance5IsAdvertisedThroughAggregateVulkan14Features) {
  // Roadmap E5: `VkPhysicalDeviceVulkan14Features.maintenance5` must agree
  // with the dedicated `VK_KHR_maintenance5` struct case below it -- every
  // other 1.4 bit remains an explicit `VK_FALSE`, pre-filled with a
  // non-zero pattern first (the same unwritten-field guard
  // `MultiviewFeaturesReportMultiviewTrueAmplificationFalse` below uses) so
  // each one is verified rather than merely a pre-existing zero.
  VkPhysicalDeviceVulkan14Features Features14;
  std::memset(&Features14, 0xAA, sizeof(Features14));
  Features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
  Features14.pNext = nullptr;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Features14;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  // Roadmap F1: now genuinely implemented (a full, mandatory priority list
  // reported for every queue family through `VkQueueFamilyGlobalPriority
  // Properties`), and must agree with the dedicated
  // `VkPhysicalDeviceGlobalPriorityQueryFeatures` struct case below.
  EXPECT_EQ(Features14.globalPriorityQuery, VK_TRUE);
  // Roadmap F2: `spirv.GroupNonUniformRotateKHR` now converts
  // (SPIRVToLLVMPatterns.cpp's `RotateConversionPattern`), covering both
  // forms with the same pattern, and must agree with the dedicated
  // `VkPhysicalDeviceShaderSubgroupRotateFeatures` struct case below.
  EXPECT_EQ(Features14.shaderSubgroupRotate, VK_TRUE);
  EXPECT_EQ(Features14.shaderSubgroupRotateClustered, VK_TRUE);
  EXPECT_EQ(Features14.shaderFloatControls2, VK_FALSE);
  // Roadmap F4: `spirv.KHR.AssumeTrue`/`spirv.KHR.Expect` now convert
  // (SPIRVToLLVMPatterns.cpp's `AssumeTrueConversionPattern`/
  // `ExpectConversionPattern`), and must agree with the dedicated
  // `VkPhysicalDeviceShaderExpectAssumeFeatures` struct case below.
  EXPECT_EQ(Features14.shaderExpectAssume, VK_TRUE);
  // Roadmap F5: `VkPipelineRasterizationLineStateCreateInfoKHR` now
  // translates (`GraphicsPipeline.cpp`) and `feme::graphics::executeDraws`
  // implements all three line styles plus stippling (Executor.cpp), and
  // must agree with the dedicated
  // `VkPhysicalDeviceLineRasterizationFeaturesKHR` struct case below.
  EXPECT_EQ(Features14.rectangularLines, VK_TRUE);
  EXPECT_EQ(Features14.bresenhamLines, VK_TRUE);
  EXPECT_EQ(Features14.smoothLines, VK_TRUE);
  EXPECT_EQ(Features14.stippledRectangularLines, VK_TRUE);
  EXPECT_EQ(Features14.stippledBresenhamLines, VK_TRUE);
  EXPECT_EQ(Features14.stippledSmoothLines, VK_TRUE);
  // Roadmap F6: `VkPipelineVertexInputDivisorStateCreateInfo`'s per-binding
  // divisor is now honored (`GraphicsPipeline.cpp`'s `translateVertexInput`,
  // Executor.cpp's fetch-index formula), and must agree with the dedicated
  // `VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR` struct case below.
  EXPECT_EQ(Features14.vertexAttributeInstanceRateDivisor, VK_TRUE);
  EXPECT_EQ(Features14.vertexAttributeInstanceRateZeroDivisor, VK_TRUE);
  // Roadmap F7: `vkCmdBindIndexBuffer`'s index read (CommandBuffer.cpp) and
  // the executor's fetch (Executor.cpp) both gained an 8-bit case, and must
  // agree with the dedicated `VkPhysicalDeviceIndexTypeUint8FeaturesKHR`
  // struct case below.
  EXPECT_EQ(Features14.indexTypeUint8, VK_TRUE);
  // Roadmap F8a: `feme::spirv::SubpassLoadPattern`/FragmentWrapper.cpp's
  // `lowerFragmentSubpassLoad` give a fragment shader's `subpassInput`
  // local read real pixels, and must agree with the dedicated
  // `VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR` struct case
  // below.
  EXPECT_EQ(Features14.dynamicRenderingLocalRead, VK_TRUE);
  // Roadmap E5: now genuinely implemented (RenderPass.cpp/
  // CommandBuffer.cpp/Format.cpp), and must agree with the dedicated
  // `VK_KHR_maintenance5` struct case below.
  EXPECT_EQ(Features14.maintenance5, VK_TRUE);
  // Roadmap E6: now genuinely implemented (vkCmdBindDescriptorSets2/
  // vkCmdPushConstants2 in CommandBuffer.cpp), and must agree with the
  // dedicated `VK_KHR_maintenance6` struct case below.
  EXPECT_EQ(Features14.maintenance6, VK_TRUE);
  // Roadmap F9: `vkCmdBindPipeline` (CommandBuffer.cpp) now honors
  // `VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT`/`VK_PIPELINE_CREATE_
  // NO_PROTECTED_ACCESS_BIT`, and must agree with the dedicated
  // `VkPhysicalDevicePipelineProtectedAccessFeatures` struct case below.
  EXPECT_EQ(Features14.pipelineProtectedAccess, VK_TRUE);
  // Roadmap F10: `VkPipelineRobustnessCreateInfo` now accepted/validated
  // at both compute and graphics pipeline creation, and must agree with
  // the dedicated `VkPhysicalDevicePipelineRobustnessFeatures` struct case
  // below.
  EXPECT_EQ(Features14.pipelineRobustness, VK_TRUE);
  // Roadmap F11: `vkCopyMemoryToImage`/`vkCopyImageToMemory`/
  // `vkCopyImageToImage`/`vkTransitionImageLayout` (HostImageCopy.cpp) are
  // now implemented, and must agree with the dedicated
  // `VkPhysicalDeviceHostImageCopyFeatures` struct case below.
  EXPECT_EQ(Features14.hostImageCopy, VK_TRUE);
  // Roadmap F12: `vkCmdPushDescriptorSet`/`vkCmdPushDescriptorSetWith
  // Template` (CommandBuffer.cpp) are now implemented; unlike the fields
  // above, `pushDescriptor` has no dedicated features struct of its own to
  // agree with (see `EntryPoints.cpp`'s own comment) -- only the dedicated
  // `VkPhysicalDevicePushDescriptorProperties` limit struct exists.
  EXPECT_EQ(Features14.pushDescriptor, VK_TRUE);
}

TEST_F(
    PhysicalDeviceProperties2Test,
    Maintenance5IsAdvertisedThroughItsOwnDedicatedFeatureAndPropertyStructs) {
  // Roadmap E5: `VK_KHR_maintenance5`'s own dedicated feature/properties
  // structs must agree with the aggregate `VkPhysicalDeviceVulkan14
  // Features`/`Properties` cases above, exactly like `VK_KHR_maintenance4`'s
  // own structs do for the 1.3 aggregate.
  VkPhysicalDeviceMaintenance5FeaturesKHR Maintenance5Features{};
  Maintenance5Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Maintenance5Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Maintenance5Features.maintenance5, VK_TRUE);

  // None of `VkPhysicalDeviceMaintenance5PropertiesKHR`'s fixed-function
  // rasterizer guarantees are verified for this software rasterizer yet,
  // matching the aggregate `VkPhysicalDeviceVulkan14Properties` case.
  VkPhysicalDeviceMaintenance5PropertiesKHR Maintenance5Props{};
  Maintenance5Props.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Maintenance5Props;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(
      Maintenance5Props.earlyFragmentMultisampleCoverageAfterSampleCounting,
      VK_FALSE);
  EXPECT_EQ(Maintenance5Props.earlyFragmentSampleMaskTestBeforeSampleCounting,
            VK_FALSE);
  EXPECT_EQ(Maintenance5Props.depthStencilSwizzleOneSupport, VK_FALSE);
  EXPECT_EQ(Maintenance5Props.polygonModePointSize, VK_FALSE);
  EXPECT_EQ(Maintenance5Props.nonStrictSinglePixelWideLinesUseParallelogram,
            VK_FALSE);
  EXPECT_EQ(Maintenance5Props.nonStrictWideLinesUseParallelogram, VK_FALSE);
}

TEST_F(PhysicalDeviceProperties2Test,
       Maintenance6IsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap E6: `VK_KHR_maintenance6`'s own dedicated feature struct must
  // agree with the aggregate `VkPhysicalDeviceVulkan14Features` case above,
  // exactly like `VK_KHR_maintenance5`'s own struct does.
  VkPhysicalDeviceMaintenance6Features Maintenance6Features{};
  Maintenance6Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Maintenance6Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Maintenance6Features.maintenance6, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       GlobalPriorityQueryIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F1: `VK_KHR_global_priority`'s own dedicated feature struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan14Features` case
  // above, exactly like `VK_KHR_maintenance6`'s own struct does.
  VkPhysicalDeviceGlobalPriorityQueryFeatures GlobalPriorityQueryFeatures{};
  GlobalPriorityQueryFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &GlobalPriorityQueryFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(GlobalPriorityQueryFeatures.globalPriorityQuery, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       ShaderSubgroupRotateIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F2: `VK_KHR_shader_subgroup_rotate`'s own dedicated feature
  // struct must agree with the aggregate `VkPhysicalDeviceVulkan14Features`
  // case above, exactly like `VK_KHR_global_priority`'s own struct does.
  VkPhysicalDeviceShaderSubgroupRotateFeatures ShaderSubgroupRotateFeatures{};
  ShaderSubgroupRotateFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &ShaderSubgroupRotateFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(ShaderSubgroupRotateFeatures.shaderSubgroupRotate, VK_TRUE);
  EXPECT_EQ(ShaderSubgroupRotateFeatures.shaderSubgroupRotateClustered,
            VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       ShaderExpectAssumeIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F4: `VK_KHR_shader_expect_assume`'s own dedicated feature
  // struct must agree with the aggregate `VkPhysicalDeviceVulkan14Features`
  // case above, exactly like `VK_KHR_shader_subgroup_rotate`'s own struct
  // does.
  VkPhysicalDeviceShaderExpectAssumeFeatures ShaderExpectAssumeFeatures{};
  ShaderExpectAssumeFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &ShaderExpectAssumeFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(ShaderExpectAssumeFeatures.shaderExpectAssume, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       LineRasterizationIsAdvertisedThroughItsOwnDedicatedStructs) {
  // Roadmap F5: `VK_KHR_line_rasterization`'s own dedicated feature and
  // properties structs must agree with the aggregate
  // `VkPhysicalDeviceVulkan14Features`/`...Vulkan14Properties` cases
  // above, exactly like `VK_KHR_shader_expect_assume`'s own structs do.
  VkPhysicalDeviceLineRasterizationFeaturesKHR LineFeatures{};
  LineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &LineFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(LineFeatures.rectangularLines, VK_TRUE);
  EXPECT_EQ(LineFeatures.bresenhamLines, VK_TRUE);
  EXPECT_EQ(LineFeatures.smoothLines, VK_TRUE);
  EXPECT_EQ(LineFeatures.stippledRectangularLines, VK_TRUE);
  EXPECT_EQ(LineFeatures.stippledBresenhamLines, VK_TRUE);
  EXPECT_EQ(LineFeatures.stippledSmoothLines, VK_TRUE);

  VkPhysicalDeviceLineRasterizationPropertiesKHR LineProperties{};
  LineProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_KHR;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &LineProperties;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(LineProperties.lineSubPixelPrecisionBits, 4u);
}

TEST_F(PhysicalDeviceProperties2Test,
       VertexAttributeDivisorIsAdvertisedThroughItsOwnDedicatedStructs) {
  // Roadmap F6: `VK_KHR_vertex_attribute_divisor`'s own dedicated feature
  // and properties structs must agree with the aggregate
  // `VkPhysicalDeviceVulkan14Features`/`...Vulkan14Properties` cases above,
  // exactly like `VK_KHR_line_rasterization`'s own structs do.
  VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR DivisorFeatures{};
  DivisorFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &DivisorFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(DivisorFeatures.vertexAttributeInstanceRateDivisor, VK_TRUE);
  EXPECT_EQ(DivisorFeatures.vertexAttributeInstanceRateZeroDivisor, VK_TRUE);

  VkPhysicalDeviceVertexAttributeDivisorPropertiesKHR DivisorProperties{};
  DivisorProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_KHR;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &DivisorProperties;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(DivisorProperties.maxVertexAttribDivisor, 0xFFFFFFFFu);
  EXPECT_EQ(DivisorProperties.supportsNonZeroFirstInstance, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       IndexTypeUint8IsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F7: `VK_KHR_index_type_uint8`'s own dedicated feature struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan14Features` case
  // above, exactly like `VK_KHR_vertex_attribute_divisor`'s own struct does.
  VkPhysicalDeviceIndexTypeUint8FeaturesKHR IndexTypeUint8Features{};
  IndexTypeUint8Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &IndexTypeUint8Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(IndexTypeUint8Features.indexTypeUint8, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       DynamicRenderingLocalReadIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F8a: `VK_KHR_dynamic_rendering_local_read`'s own dedicated
  // feature struct must agree with the aggregate
  // `VkPhysicalDeviceVulkan14Features` case above, exactly like
  // `VK_KHR_index_type_uint8`'s own struct does.
  VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR LocalReadFeatures{};
  LocalReadFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &LocalReadFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(LocalReadFeatures.dynamicRenderingLocalRead, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       EveryQueueFamilyReportsTheFullMandatoryGlobalPriorityList) {
  // Roadmap F1: this ICD has one worker pool with no real OS-level
  // scheduling priority, so every queue family reports every priority
  // level the spec defines as supported, sorted ascending -- the same
  // "single logical queue, narrowed by capability flags only" precedent
  // roadmap C7 set for `queueFlags` applies equally here: there is no real
  // per-priority distinction for this executor to narrow.
  uint32_t Count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties2(Physical, &Count, nullptr);
  ASSERT_EQ(Count, PhysicalDeviceInfo::NumQueueFamilies);

  std::vector<VkQueueFamilyGlobalPriorityProperties> Priorities(Count);
  std::vector<VkQueueFamilyProperties2> Families(Count);
  for (uint32_t I = 0; I < Count; ++I) {
    Priorities[I].sType =
        VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES;
    Priorities[I].pNext = nullptr;
    Families[I].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    Families[I].pNext = &Priorities[I];
  }
  vkGetPhysicalDeviceQueueFamilyProperties2(Physical, &Count, Families.data());

  for (uint32_t I = 0; I < Count; ++I) {
    ASSERT_EQ(Priorities[I].priorityCount, 4u);
    EXPECT_EQ(Priorities[I].priorities[0], VK_QUEUE_GLOBAL_PRIORITY_LOW);
    EXPECT_EQ(Priorities[I].priorities[1], VK_QUEUE_GLOBAL_PRIORITY_MEDIUM);
    EXPECT_EQ(Priorities[I].priorities[2], VK_QUEUE_GLOBAL_PRIORITY_HIGH);
    EXPECT_EQ(Priorities[I].priorities[3], VK_QUEUE_GLOBAL_PRIORITY_REALTIME);
  }
}

TEST_F(PhysicalDeviceProperties2Test,
       Maintenance6PropertiesReportARealCombinedImageSamplerCount) {
  // Roadmap E6: `VK_KHR_maintenance6`'s own dedicated properties struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan14Properties`
  // case above. `maxCombinedImageSamplerDescriptorCount` is a real value
  // (1, since this ICD supports no multi-planar/YCbCr samplers); the other
  // two fields describe unrelated, still-unimplemented fixed-function
  // guarantees and stay `VK_FALSE`.
  VkPhysicalDeviceMaintenance6Properties Maintenance6Props{};
  Maintenance6Props.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &Maintenance6Props;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(Maintenance6Props.blockTexelViewCompatibleMultipleLayers, VK_FALSE);
  EXPECT_EQ(Maintenance6Props.maxCombinedImageSamplerDescriptorCount, 1u);
  EXPECT_EQ(Maintenance6Props.fragmentShadingRateClampCombinerInputs, VK_FALSE);

  VkPhysicalDeviceVulkan14Properties Props14{};
  Props14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
  Props2.pNext = &Props14;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(Maintenance6Props.maxCombinedImageSamplerDescriptorCount,
            Props14.maxCombinedImageSamplerDescriptorCount);
}

TEST_F(PhysicalDeviceProperties2Test,
       PipelineRobustnessIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F10: `VK_EXT_pipeline_robustness`'s own dedicated feature
  // struct must agree with the aggregate `VkPhysicalDeviceVulkan14Features`
  // case, exactly like `VK_KHR_global_priority`'s own struct does.
  VkPhysicalDevicePipelineRobustnessFeatures PipelineRobustnessFeatures{};
  PipelineRobustnessFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &PipelineRobustnessFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(PipelineRobustnessFeatures.pipelineRobustness, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       PipelineRobustnessPropertiesReportRealDefaultBehaviors) {
  // Roadmap F10: `VK_EXT_pipeline_robustness`'s own dedicated properties
  // struct must agree with the aggregate `VkPhysicalDeviceVulkan14Properties`
  // case above, exactly like `VK_KHR_maintenance6`'s own struct does. Every
  // field is a real, non-`..._DEVICE_DEFAULT` value describing this
  // device's actual out-of-bounds behavior with no robustness feature
  // enabled (see EntryPoints.cpp's own comment for why `..._ROBUST_BUFFER_
  // ACCESS`/`..._ROBUST_IMAGE_ACCESS` rather than their stronger `..._2`
  // siblings).
  VkPhysicalDevicePipelineRobustnessProperties PipelineRobustnessProps{};
  PipelineRobustnessProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &PipelineRobustnessProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessStorageBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessUniformBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessVertexInputs,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessImages,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS);

  VkPhysicalDeviceVulkan14Properties Props14{};
  Props14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
  Props2.pNext = &Props14;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessStorageBuffers,
            Props14.defaultRobustnessStorageBuffers);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessUniformBuffers,
            Props14.defaultRobustnessUniformBuffers);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessVertexInputs,
            Props14.defaultRobustnessVertexInputs);
  EXPECT_EQ(PipelineRobustnessProps.defaultRobustnessImages,
            Props14.defaultRobustnessImages);
}

TEST_F(PhysicalDeviceProperties2Test,
       HostImageCopyIsAdvertisedThroughItsOwnDedicatedFeatureStruct) {
  // Roadmap F11: `VK_EXT_host_image_copy`'s own dedicated feature struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan14Features` case,
  // exactly like `VK_EXT_pipeline_robustness`'s own struct does.
  VkPhysicalDeviceHostImageCopyFeatures HostImageCopyFeatures{};
  HostImageCopyFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &HostImageCopyFeatures;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(HostImageCopyFeatures.hostImageCopy, VK_TRUE);
}

TEST_F(PhysicalDeviceProperties2Test,
       HostImageCopyPropertiesReportSupportedLayoutsAndRealDefaults) {
  // Roadmap F11: `VK_EXT_host_image_copy`'s own dedicated properties struct
  // must agree with the aggregate `VkPhysicalDeviceVulkan14Properties` case
  // above, exactly like `VK_EXT_pipeline_robustness`'s own struct does.
  VkPhysicalDeviceHostImageCopyProperties HostImageCopyProps{};
  HostImageCopyProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &HostImageCopyProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  ASSERT_EQ(HostImageCopyProps.copySrcLayoutCount, 2u);
  ASSERT_NE(HostImageCopyProps.pCopySrcLayouts, nullptr);
  EXPECT_EQ(HostImageCopyProps.pCopySrcLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(HostImageCopyProps.pCopySrcLayouts[1],
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  ASSERT_EQ(HostImageCopyProps.copyDstLayoutCount, 2u);
  ASSERT_NE(HostImageCopyProps.pCopyDstLayouts, nullptr);
  EXPECT_EQ(HostImageCopyProps.pCopyDstLayouts[0], VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(HostImageCopyProps.pCopyDstLayouts[1],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  // This ICD has exactly one memory type, so an image created with or
  // without `VK_IMAGE_CREATE_HOST_IMAGE_COPY_BIT` always has identical
  // memory type requirements.
  EXPECT_EQ(HostImageCopyProps.identicalMemoryTypeRequirements, VK_TRUE);
  uint8_t ZeroUUID[VK_UUID_SIZE] = {};
  EXPECT_NE(std::memcmp(HostImageCopyProps.optimalTilingLayoutUUID, ZeroUUID,
                        VK_UUID_SIZE),
            0);

  VkPhysicalDeviceVulkan14Properties Props14{};
  Props14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
  Props2.pNext = &Props14;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(HostImageCopyProps.copySrcLayoutCount, Props14.copySrcLayoutCount);
  EXPECT_EQ(HostImageCopyProps.pCopySrcLayouts, Props14.pCopySrcLayouts);
  EXPECT_EQ(HostImageCopyProps.copyDstLayoutCount, Props14.copyDstLayoutCount);
  EXPECT_EQ(HostImageCopyProps.pCopyDstLayouts, Props14.pCopyDstLayouts);
  EXPECT_EQ(std::memcmp(HostImageCopyProps.optimalTilingLayoutUUID,
                        Props14.optimalTilingLayoutUUID, VK_UUID_SIZE),
            0);
  EXPECT_EQ(HostImageCopyProps.identicalMemoryTypeRequirements,
            Props14.identicalMemoryTypeRequirements);
}

TEST_F(PhysicalDeviceProperties2Test,
       PushDescriptorPropertiesReportTheMandatoryMinimum) {
  // Roadmap F12: `VK_KHR_push_descriptor`'s own dedicated properties
  // struct must agree with the aggregate `VkPhysicalDeviceVulkan14Properties`
  // case above, exactly like `VK_EXT_host_image_copy`'s own struct does.
  VkPhysicalDevicePushDescriptorProperties PushDescriptorProps{};
  PushDescriptorProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &PushDescriptorProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  // 32 is the spec-mandated minimum `maxPushDescriptors`; this ICD's push
  // descriptor set is an ordinary `DescriptorSet` with no smaller heap of
  // its own to cap it further, so the minimum is also the real value.
  EXPECT_EQ(PushDescriptorProps.maxPushDescriptors, 32u);

  VkPhysicalDeviceVulkan14Properties Props14{};
  Props14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
  Props2.pNext = &Props14;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);
  EXPECT_EQ(PushDescriptorProps.maxPushDescriptors, Props14.maxPushDescriptors);
}

TEST_F(PhysicalDeviceProperties2Test,
       MultiviewFeaturesReportMultiviewTrueAmplificationFalse) {
  // Roadmap H2: `multiview` is now real (layered rendering/multiview,
  // RenderPass.cpp/CommandBuffer.cpp) and reported `VK_TRUE`;
  // `multiviewGeometryShader`/`multiviewTessellationShader` stay an
  // explicit `VK_FALSE` -- guarded here by pre-filling with a non-zero
  // pattern before the call, the same pattern
  // `dEQP-VK.api.info.vulkan1p2.features`/`multiview_features` use to
  // catch an unwritten field -- since neither a geometry nor a
  // tessellation-evaluation stage exists yet (roadmap H4/H5).
  VkPhysicalDeviceMultiviewFeatures Multiview;
  std::memset(&Multiview, 0xAA, sizeof(Multiview));
  Multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
  Multiview.pNext = nullptr;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Multiview;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  EXPECT_EQ(Multiview.multiview, VK_TRUE);
  EXPECT_EQ(Multiview.multiviewGeometryShader, VK_FALSE);
  EXPECT_EQ(Multiview.multiviewTessellationShader, VK_FALSE);
}

TEST_F(PhysicalDeviceProperties2Test,
       IdPropertiesMatchPromotedVulkan11Properties) {
  // Roadmap C6: closing this promoted-struct disagreement was a
  // prerequisite for `vulkan1p2.property_extensions_consistency` (see
  // EntryPoints.cpp's `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES`
  // case comment).
  VkPhysicalDeviceIDProperties IdProps{};
  IdProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  VkPhysicalDeviceVulkan11Properties Props11{};
  Props11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
  IdProps.pNext = &Props11;

  VkPhysicalDeviceProperties2 Props2{};
  Props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  Props2.pNext = &IdProps;
  vkGetPhysicalDeviceProperties2(Physical, &Props2);

  EXPECT_EQ(std::memcmp(IdProps.deviceUUID, Props11.deviceUUID, VK_UUID_SIZE),
            0);
  EXPECT_EQ(std::memcmp(IdProps.driverUUID, Props11.driverUUID, VK_UUID_SIZE),
            0);
  EXPECT_EQ(IdProps.deviceLUIDValid, Props11.deviceLUIDValid);
  EXPECT_EQ(IdProps.deviceLUIDValid, VK_FALSE);
}

} // namespace
