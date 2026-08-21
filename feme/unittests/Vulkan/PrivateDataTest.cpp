//===- PrivateDataTest.cpp - VkPrivateDataSlot tests ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "PrivateData.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

class PrivateDataTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
};

TEST_F(PrivateDataTest, CreateAndDestroyEmptySlot) {
  VkPrivateDataSlotCreateInfo CreateInfo{};
  VkPrivateDataSlot Slot = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &Slot),
            VK_SUCCESS);
  EXPECT_NE(Slot, VK_NULL_HANDLE);
  vkDestroyPrivateDataSlot(Device, Slot, nullptr);
}

/// `vkGetPrivateData` on a fresh slot must report 0 for any object that has
/// never had a value set, per the extension's own default.
TEST_F(PrivateDataTest, UnsetValueReadsBackZero) {
  VkPrivateDataSlotCreateInfo CreateInfo{};
  VkPrivateDataSlot Slot = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &Slot),
            VK_SUCCESS);

  uint64_t Data = 0xdeadbeef;
  vkGetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1234, Slot, &Data);
  EXPECT_EQ(Data, 0u);

  vkDestroyPrivateDataSlot(Device, Slot, nullptr);
}

/// A value set for one `(objectType, objectHandle)` round-trips through
/// `vkGetPrivateData`.
TEST_F(PrivateDataTest, SetAndGetRoundTrips) {
  VkPrivateDataSlotCreateInfo CreateInfo{};
  VkPrivateDataSlot Slot = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &Slot),
            VK_SUCCESS);

  ASSERT_EQ(vkSetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1234, Slot,
                             0xabcdef01),
            VK_SUCCESS);
  uint64_t Data = 0;
  vkGetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1234, Slot, &Data);
  EXPECT_EQ(Data, 0xabcdef01u);

  // Setting a new value for the same key overwrites the old one.
  ASSERT_EQ(vkSetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1234, Slot,
                             0x11223344),
            VK_SUCCESS);
  vkGetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1234, Slot, &Data);
  EXPECT_EQ(Data, 0x11223344u);

  vkDestroyPrivateDataSlot(Device, Slot, nullptr);
}

/// The same numeric handle value under two different `VkObjectType`s is a
/// distinct key -- this ICD's handles are not guaranteed unique across
/// object types, so the slot must key on both, not the handle alone.
TEST_F(PrivateDataTest, SameHandleValueDifferentObjectTypesAreDistinctKeys) {
  VkPrivateDataSlotCreateInfo CreateInfo{};
  VkPrivateDataSlot Slot = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &Slot),
            VK_SUCCESS);

  ASSERT_EQ(
      vkSetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x42, Slot, 111),
      VK_SUCCESS);
  ASSERT_EQ(vkSetPrivateData(Device, VK_OBJECT_TYPE_IMAGE, 0x42, Slot, 222),
            VK_SUCCESS);

  uint64_t BufferData = 0, ImageData = 0;
  vkGetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x42, Slot, &BufferData);
  vkGetPrivateData(Device, VK_OBJECT_TYPE_IMAGE, 0x42, Slot, &ImageData);
  EXPECT_EQ(BufferData, 111u);
  EXPECT_EQ(ImageData, 222u);

  vkDestroyPrivateDataSlot(Device, Slot, nullptr);
}

/// Two distinct slots keep independent maps: a value set in one is invisible
/// through the other, even for the same `(objectType, objectHandle)` key.
TEST_F(PrivateDataTest, DistinctSlotsAreIndependent) {
  VkPrivateDataSlotCreateInfo CreateInfo{};
  VkPrivateDataSlot First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &First),
            VK_SUCCESS);
  ASSERT_EQ(vkCreatePrivateDataSlot(Device, &CreateInfo, nullptr, &Second),
            VK_SUCCESS);

  ASSERT_EQ(
      vkSetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1, First, 999),
      VK_SUCCESS);

  uint64_t Data = 0xff;
  vkGetPrivateData(Device, VK_OBJECT_TYPE_BUFFER, 0x1, Second, &Data);
  EXPECT_EQ(Data, 0u);

  vkDestroyPrivateDataSlot(Device, First, nullptr);
  vkDestroyPrivateDataSlot(Device, Second, nullptr);
}

} // namespace
