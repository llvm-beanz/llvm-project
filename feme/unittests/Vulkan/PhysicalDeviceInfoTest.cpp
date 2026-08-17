//===- PhysicalDeviceInfoTest.cpp - Truthful capability tests --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PhysicalDeviceInfo.h"

#include "gtest/gtest.h"

#include <cstring>

using namespace feme::vulkan;

namespace {

TEST(PhysicalDeviceInfo, ReportsHonestVersionAndIdentity) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();

  // See "Loader Integration"'s V0 Deviation note: 1.1, not 1.2 (which would
  // require VkPhysicalDeviceDriverProperties to be queryable).
  EXPECT_EQ(Info.Properties.apiVersion, VK_API_VERSION_1_1);

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
}

TEST(PhysicalDeviceInfo, ComputeQueueFamilyIsComputeAndTransferOnly) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  const VkQueueFamilyProperties &Family = Info.ComputeQueueFamily;
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_COMPUTE_BIT);
  EXPECT_TRUE(Family.queueFlags & VK_QUEUE_TRANSFER_BIT);
  // No graphics queue family exists yet (see "Queue families").
  EXPECT_FALSE(Family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
  EXPECT_GE(Family.queueCount, 1u);
  // Timestamp queries are not implemented yet.
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

TEST(PhysicalDeviceInfo, NoFeatureIsAdvertisedYet) {
  // "No shader execution is required in this milestone": nothing has been
  // implemented that could honestly back a `VkBool32` feature yet.
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  VkPhysicalDeviceFeatures Zero{};
  EXPECT_EQ(std::memcmp(&Info.Features, &Zero, sizeof(Zero)), 0);
}

TEST(PhysicalDeviceInfo, DeviceAndPipelineCacheUUIDsDiffer) {
  PhysicalDeviceInfo Info = computePhysicalDeviceInfo();
  EXPECT_NE(std::memcmp(Info.DeviceUUID, Info.Properties.pipelineCacheUUID,
                        VK_UUID_SIZE),
            0);
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
