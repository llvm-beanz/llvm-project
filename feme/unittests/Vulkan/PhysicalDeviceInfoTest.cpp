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

using namespace feme::vulkan;

namespace {

TEST(PhysicalDeviceInfo, ReportsHonestVersionAndIdentity) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();

  EXPECT_EQ(Info.Properties.apiVersion, VK_API_VERSION_1_2);

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

TEST(PhysicalDeviceInfo, OnlyRobustBufferAccessAndDualSrcBlendAreAdvertised) {
  // (V4/C4) `robustBufferAccess`/`dualSrcBlend` are the only core features
  // this milestone can honestly claim (see PhysicalDeviceInfo.cpp's
  // comment); every other `VkBool32` stays false, since nothing else has
  // been implemented that could back one yet.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_EQ(Info.Features.robustBufferAccess, VK_TRUE);
  EXPECT_EQ(Info.Features.dualSrcBlend, VK_TRUE);

  VkPhysicalDeviceFeatures Cleared = Info.Features;
  Cleared.robustBufferAccess = VK_FALSE;
  Cleared.dualSrcBlend = VK_FALSE;
  VkPhysicalDeviceFeatures Zero{};
  EXPECT_EQ(std::memcmp(&Cleared, &Zero, sizeof(Zero)), 0);
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
       MultiviewFeaturesAreExplicitlyFalseNotLeftUnwritten) {
  // Roadmap C6: `multiview` cannot be honestly advertised yet (layered
  // rendering is V7), but every field must still be an explicit `VK_FALSE`
  // rather than whatever the caller's own buffer held -- guarded here by
  // pre-filling with a non-zero pattern before the call, the same pattern
  // `dEQP-VK.api.info.vulkan1p2.features`/`multiview_features` use to
  // catch an unwritten field.
  VkPhysicalDeviceMultiviewFeatures Multiview;
  std::memset(&Multiview, 0xAA, sizeof(Multiview));
  Multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
  Multiview.pNext = nullptr;

  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Multiview;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);

  EXPECT_EQ(Multiview.multiview, VK_FALSE);
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
