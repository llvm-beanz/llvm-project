//===- MemoryTest.cpp - VkDeviceMemory tests -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Memory.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "gtest/gtest.h"

#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#endif

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

// Roadmap L416 phase 2: a real vkAllocateMemory-created VkDeviceMemory is
// now backed by a shareable memory object (see Memory.h's own file
// comment), not a plain heap allocation -- a precondition a future
// session's vkQueueBindSparse needs to actually remap pages. Nothing
// reads DeviceMemory::shareableFd() yet outside tests.
TEST_F(MemoryTest, AllocateMemoryProducesShareableFd) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 256;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  DeviceMemory *Obj = fromHandle<DeviceMemory>(Memory);
#ifndef _WIN32
  // On a POSIX host, `createShareableMemoryFd` is implemented, so this
  // allocation's handle must be a real, usable shareable object.
  EXPECT_GE(Obj->shareableFd(), 0);
#endif

  vkFreeMemory(Device, Memory, nullptr);
}

#ifndef _WIN32
// The whole point of backing `VkDeviceMemory` with a shareable memory
// object rather than a plain heap allocation: its pages can be mapped a
// *second* time, independently, at another address, and both mappings
// observe each other's writes -- exactly what a future
// `vkQueueBindSparse` needs to alias a sparse buffer's reserved
// virtual-address window onto the same physical storage `vkMapMemory`'s
// own mapping already uses.
TEST_F(MemoryTest, AllocateMemoryShareableFdCanBeRemappedAndAliases) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 4096;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  void *MappedData = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &MappedData),
            VK_SUCCESS);

  DeviceMemory *Obj = fromHandle<DeviceMemory>(Memory);
  ASSERT_GE(Obj->shareableFd(), 0);

  void *SecondView = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
                          static_cast<int>(Obj->shareableFd()), 0);
  ASSERT_NE(SecondView, MAP_FAILED);

  std::memset(MappedData, 0x5A, 4096);
  EXPECT_EQ(static_cast<uint8_t *>(SecondView)[0], 0x5A);
  EXPECT_EQ(static_cast<uint8_t *>(SecondView)[4095], 0x5A);

  munmap(SecondView, 4096);
  vkFreeMemory(Device, Memory, nullptr);
}
#endif

// Roadmap L236(a): vkGetDeviceMemoryOpaqueCaptureAddress must resolve via
// vkGetDeviceProcAddr regardless of bufferDeviceAddressCaptureReplay's
// (always-false) feature bit -- see its own comment (Memory.cpp) for why it
// returns the allocation's host pointer.
TEST_F(MemoryTest, GetDeviceMemoryOpaqueCaptureAddressMatchesHostPointer) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Memory, 0, VK_WHOLE_SIZE, 0, &Data),
            VK_SUCCESS);

  VkDeviceMemoryOpaqueCaptureAddressInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO;
  Info.memory = Memory;
  EXPECT_EQ(reinterpret_cast<void *>(
                vkGetDeviceMemoryOpaqueCaptureAddress(Device, &Info)),
            Data);

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

// Roadmap F14: VK_KHR_map_memory2's vkMapMemory2/vkUnmapMemory2 wrap the
// same logic as vkMapMemory/vkUnmapMemory above, driven by a
// VkMemoryMapInfo/VkMemoryUnmapInfo struct instead of separate arguments.
TEST_F(MemoryTest, MapMemory2WriteUnmap) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 256;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);
  ASSERT_NE(Memory, VK_NULL_HANDLE);

  VkMemoryMapInfo MapInfo{};
  MapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
  MapInfo.memory = Memory;
  MapInfo.offset = 0;
  MapInfo.size = VK_WHOLE_SIZE;

  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory2(Device, &MapInfo, &Data), VK_SUCCESS);
  ASSERT_NE(Data, nullptr);
  std::memset(Data, 0x42, 256);
  EXPECT_EQ(static_cast<uint8_t *>(Data)[255], 0x42);

  VkMemoryUnmapInfo UnmapInfo{};
  UnmapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO;
  UnmapInfo.memory = Memory;
  EXPECT_EQ(vkUnmapMemory2(Device, &UnmapInfo), VK_SUCCESS);

  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(MemoryTest, MapMemory2RejectsOutOfRange) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  VkMemoryMapInfo MapInfo{};
  MapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
  MapInfo.memory = Memory;
  MapInfo.offset = 32;
  MapInfo.size = 64;

  void *Data = nullptr;
  EXPECT_EQ(vkMapMemory2(Device, &MapInfo, &Data), VK_ERROR_MEMORY_MAP_FAILED);

  vkFreeMemory(Device, Memory, nullptr);
}

// `VK_MEMORY_UNMAP_RESERVE_BIT_EXT` only has meaning together with
// `VK_EXT_map_memory_placed` (unimplemented); this driver accepts it as a
// validate-only no-op rather than rejecting it.
TEST_F(MemoryTest, UnmapMemory2AcceptsReserveBit) {
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);

  VkMemoryMapInfo MapInfo{};
  MapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
  MapInfo.memory = Memory;
  MapInfo.offset = 0;
  MapInfo.size = VK_WHOLE_SIZE;
  void *Data = nullptr;
  ASSERT_EQ(vkMapMemory2(Device, &MapInfo, &Data), VK_SUCCESS);

  VkMemoryUnmapInfo UnmapInfo{};
  UnmapInfo.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO;
  UnmapInfo.flags = VK_MEMORY_UNMAP_RESERVE_BIT_EXT;
  UnmapInfo.memory = Memory;
  EXPECT_EQ(vkUnmapMemory2(Device, &UnmapInfo), VK_SUCCESS);

  vkFreeMemory(Device, Memory, nullptr);
}

// Low-level tests of `allocateShareableDeviceMemory`/
// `freeShareableDeviceMemory` themselves, independent of any
// `VkDevice`/`VkDeviceMemory` machinery (mirrors
// `VirtualAddressReservationTest.cpp`'s own standalone-helper style).
TEST(AllocateShareableDeviceMemoryTest, AllocateReadWriteFree) {
  intptr_t Fd = -1;
  void *Data = allocateShareableDeviceMemory(4096, 64, &Fd);
  ASSERT_NE(Data, nullptr);
#ifndef _WIN32
  EXPECT_GE(Fd, 0);
#endif

  std::memset(Data, 0x7E, 4096);
  EXPECT_EQ(static_cast<uint8_t *>(Data)[0], 0x7E);
  EXPECT_EQ(static_cast<uint8_t *>(Data)[4095], 0x7E);

  freeShareableDeviceMemory(Data, 4096, Fd);
}

TEST(AllocateShareableDeviceMemoryTest, FreeToleratesFallbackFd) {
  // `Fd == -1` is the documented fallback-path case (equivalent to
  // `freeDeviceMemory`); exercised directly here since the POSIX success
  // path above never produces it.
  void *Data = allocateDeviceMemory(64, 64);
  ASSERT_NE(Data, nullptr);
  freeShareableDeviceMemory(Data, 64, -1);
}

} // namespace
