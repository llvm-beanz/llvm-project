//===- BufferTest.cpp - VkBuffer tests --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Buffer.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

class BufferTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
};

TEST_F(BufferTest, CreateBindDestroy) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 128;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  ASSERT_NE(Buf, VK_NULL_HANDLE);

  VkMemoryRequirements Reqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
  EXPECT_GE(Reqs.size, 128u);
  EXPECT_EQ(Reqs.memoryTypeBits, 0x1u);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);

  EXPECT_EQ(vkBindBufferMemory(Device, Buf, Memory, 0), VK_SUCCESS);

  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Data),
            VK_SUCCESS);
  EXPECT_EQ(Data, fromHandle<Buffer>(Buf)->data());

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(BufferTest, BindBufferMemory2) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 64;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);

  VkBindBufferMemoryInfo BindInfo{};
  BindInfo.buffer = Buf;
  BindInfo.memory = Memory;
  BindInfo.memoryOffset = 0;
  EXPECT_EQ(vkBindBufferMemory2(Device, 1, &BindInfo), VK_SUCCESS);
  EXPECT_TRUE(fromHandle<Buffer>(Buf)->isBound());

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(BufferTest, RejectsZeroSize) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 0;
  VkBuffer Buf = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf),
            VK_ERROR_INITIALIZATION_FAILED);
}

} // namespace
