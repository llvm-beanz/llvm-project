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

using namespace feme::vulkan;

namespace {

TEST(PhysicalDeviceInfo, ReportsHonestVersionAndIdentity) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();

  // See "Loader Integration"'s V0 Deviation note: 1.1, not 1.2 (which would
  // require VkPhysicalDeviceDriverProperties to be queryable).
  EXPECT_EQ(Info.Properties.apiVersion, VK_API_VERSION_1_2);

  // The Khronos "not yet assigned an official vendor ID" reserved value
  // (see "Device identity").
  EXPECT_EQ(Info.Properties.vendorID, 0x10000u);
  EXPECT_EQ(Info.DriverId, VK_DRIVER_ID_MAX_ENUM);

  // Zero `VkConformanceVersion` (see "Initial Non-Goals": no conformance
  // claim). `VkPhysicalDeviceProperties` itself has no conformance field
  // (that lives in `VkPhysicalDeviceVulkan12Properties`, not queryable at
  // this milestone's apiVersion), so this documents the intended contract
  // for whichever milestone first claims Vulkan 1.2.
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
  const VkQueueFamilyProperties &Family = Info.UniversalQueueFamily;
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

TEST(PhysicalDeviceInfo, IsDeterministic) {
  PhysicalDeviceInfo A = computePhysicalDeviceInfo();
  PhysicalDeviceInfo B = computePhysicalDeviceInfo();
  EXPECT_EQ(A.SubgroupSize, B.SubgroupSize);
  EXPECT_EQ(std::memcmp(A.DeviceUUID, B.DeviceUUID, VK_UUID_SIZE), 0);
  EXPECT_EQ(std::memcmp(A.Properties.pipelineCacheUUID,
                        B.Properties.pipelineCacheUUID, VK_UUID_SIZE),
            0);
}

} // namespace
