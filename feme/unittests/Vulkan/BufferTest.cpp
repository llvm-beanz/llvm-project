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
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

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
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

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

class BufferViewTest : public BufferTest {
protected:
  void SetUp() override {
    BufferTest::SetUp();
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = 64;
    BufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = 64;
    AllocInfo.memoryTypeIndex = 0;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindBufferMemory(Device, Buf, Memory, 0), VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyBuffer(Device, Buf, nullptr);
    vkFreeMemory(Device, Memory, nullptr);
    BufferTest::TearDown();
  }

  VkBuffer Buf = VK_NULL_HANDLE;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
};

/// Every format the CPU runtime's typed-load/store helpers implement a
/// conversion for is usable in a texel buffer's `VkBufferView` -- see
/// `feme::vulkan::isTexelBufferFormatSupported`'s comment.
TEST_F(BufferViewTest, AcceptsRuntimeSupportedFormats) {
  for (VkFormat Format :
       {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R8G8B8A8_UNORM}) {
    VkBufferViewCreateInfo ViewInfo{};
    ViewInfo.buffer = Buf;
    ViewInfo.format = Format;
    ViewInfo.range = VK_WHOLE_SIZE;
    VkBufferView View = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateBufferView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS)
        << "format " << Format;
    vkDestroyBufferView(Device, View, nullptr);
  }
}

/// A format `mapVkFormat` itself recognizes, but that the CPU runtime has no
/// typed-load/store conversion for, must still be rejected -- silently
/// misconverting it would be worse than refusing to create the view at all.
TEST_F(BufferViewTest, RejectsFormatWithNoRuntimeConversion) {
  VkBufferViewCreateInfo ViewInfo{};
  ViewInfo.buffer = Buf;
  ViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  ViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView View = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateBufferView(Device, &ViewInfo, nullptr, &View),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
  EXPECT_EQ(View, VK_NULL_HANDLE);
}

TEST_F(BufferViewTest, RejectsUnknownFormat) {
  VkBufferViewCreateInfo ViewInfo{};
  ViewInfo.buffer = Buf;
  ViewInfo.format = VK_FORMAT_UNDEFINED;
  ViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView View = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateBufferView(Device, &ViewInfo, nullptr, &View),
            VK_ERROR_FORMAT_NOT_SUPPORTED);
}

} // namespace
