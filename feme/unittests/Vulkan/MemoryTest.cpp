//===- MemoryTest.cpp - VkDeviceMemory tests -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <cstring>

using namespace feme::vulkan;

namespace {

class MemoryTest : public ::testing::Test {
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

TEST_F(MemoryTest, AllocateMapWriteFree) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 256;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);
  ASSERT_NE(Memory, VK_NULL_HANDLE);

  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Data),
            VK_SUCCESS);
  ASSERT_NE(Data, nullptr);
  std::memset(Data, 0x42, 256);
  EXPECT_EQ(static_cast<uint8_t *>(Data)[255], 0x42);
  vkUnmapMemory(Device, Memory);

  VkMappedMemoryRange Range{};
  Range.memory = Memory;
  Range.offset = 0;
  Range.size = VK_WHOLE_SIZE;
  EXPECT_EQ(vkFlushMappedMemoryRanges(Device, 1, &Range), VK_SUCCESS);
  EXPECT_EQ(vkInvalidateMappedMemoryRanges(Device, 1, &Range), VK_SUCCESS);

  VkDeviceSize Committed = 0;
  vkGetDeviceMemoryCommitment(Device, Memory, &Committed);
  EXPECT_EQ(Committed, 256u);

  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(MemoryTest, RejectsUnknownMemoryType) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 256;
  AllocInfo.memoryTypeIndex = 1; // Only type 0 exists.

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(MemoryTest, MapRejectsOutOfRange) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  void *Data = nullptr;
  EXPECT_EQ(vkMapMemory(Device, Memory, 32, 64, 0, &Data),
            VK_ERROR_MEMORY_MAP_FAILED);

  vkFreeMemory(Device, Memory, nullptr);
}

} // namespace
