//===- ObjectModelTest.cpp - Instance/Device/Queue tests -------*- C++ -*-===//
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

#include <cstdlib>

using namespace feme::vulkan;

namespace {

/// A `VkAllocationCallbacks` that counts allocations and frees, so tests can
/// check every allocated object is freed (see "Object Model": "All objects
/// use the application's `VkAllocationCallbacks` when supplied").
struct CountingAllocator {
  int Allocations = 0;
  int Frees = 0;

  static void *VKAPI_PTR alloc(void *pUserData, size_t Size, size_t Alignment,
                               VkSystemAllocationScope) {
    auto *Self = static_cast<CountingAllocator *>(pUserData);
    ++Self->Allocations;
#if defined(_ISOC11_SOURCE)
    return aligned_alloc(Alignment, Size);
#else
    void *Ptr = nullptr;
    if (posix_memalign(&Ptr,
                       Alignment < sizeof(void *) ? sizeof(void *) : Alignment,
                       Size) != 0)
      return nullptr;
    return Ptr;
#endif
  }
  static void VKAPI_PTR free(void *pUserData, void *Ptr) {
    auto *Self = static_cast<CountingAllocator *>(pUserData);
    if (Ptr)
      ++Self->Frees;
    std::free(Ptr);
  }
  static void *VKAPI_PTR realloc(void *, void *, size_t, size_t,
                                 VkSystemAllocationScope) {
    // Unused by this ICD (objects are only ever created at their final
    // size), but Vulkan requires every callback to be non-null.
    return nullptr;
  }
  static void VKAPI_PTR internalAlloc(void *, size_t, VkInternalAllocationType,
                                      VkSystemAllocationScope) {}
  static void VKAPI_PTR internalFree(void *, size_t, VkInternalAllocationType,
                                     VkSystemAllocationScope) {}

  VkAllocationCallbacks callbacks() {
    VkAllocationCallbacks CB{};
    CB.pUserData = this;
    CB.pfnAllocation = &alloc;
    CB.pfnReallocation = &realloc;
    CB.pfnFree = &free;
    CB.pfnInternalAllocation = &internalAlloc;
    CB.pfnInternalFree = &internalFree;
    return CB;
  }
};

VkInstance createInstance(const VkAllocationCallbacks *Callbacks = nullptr) {
  VkInstanceCreateInfo CreateInfo{};
  VkInstance Instance = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateInstance(&CreateInfo, Callbacks, &Instance), VK_SUCCESS);
  return Instance;
}

TEST(ObjectModel, CreateDestroyInstance) {
  VkInstance Instance = createInstance();
  ASSERT_NE(Instance, VK_NULL_HANDLE);
  vkDestroyInstance(Instance, nullptr);
}

TEST(ObjectModel, CreateInstanceRejectsUnknownExtension) {
  VkInstanceCreateInfo CreateInfo{};
  const char *Ext = "VK_EXT_not_implemented";
  CreateInfo.enabledExtensionCount = 1;
  CreateInfo.ppEnabledExtensionNames = &Ext;
  VkInstance Instance = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateInstance(&CreateInfo, nullptr, &Instance),
            VK_ERROR_EXTENSION_NOT_PRESENT);
}

TEST(ObjectModel, InstanceUsesSuppliedAllocator) {
  CountingAllocator Counter;
  VkAllocationCallbacks CB = Counter.callbacks();
  VkInstance Instance = createInstance(&CB);
  ASSERT_NE(Instance, VK_NULL_HANDLE);
  EXPECT_EQ(Counter.Allocations, 1);
  vkDestroyInstance(Instance, &CB);
  EXPECT_EQ(Counter.Frees, 1);
}

TEST(ObjectModel, EnumeratesExactlyOnePhysicalDevice) {
  VkInstance Instance = createInstance();
  uint32_t Count = 0;
  EXPECT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, nullptr), VK_SUCCESS);
  EXPECT_EQ(Count, 1u);

  VkPhysicalDevice Device = VK_NULL_HANDLE;
  EXPECT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Device), VK_SUCCESS);
  EXPECT_NE(Device, VK_NULL_HANDLE);
  vkDestroyInstance(Instance, nullptr);
}

TEST(ObjectModel, EnumerateReportsIncompleteWhenTruncated) {
  VkInstance Instance = createInstance();
  uint32_t Count = 0; // Ask for zero entries with a non-null buffer.
  VkPhysicalDevice Device = VK_NULL_HANDLE;
  EXPECT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Device),
            VK_INCOMPLETE);
  EXPECT_EQ(Count, 0u);
  vkDestroyInstance(Instance, nullptr);
}

class DeviceTest : public ::testing::Test {
protected:
  void SetUp() override {
    Instance = createInstance();
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
  }
  void TearDown() override { vkDestroyInstance(Instance, nullptr); }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
};

TEST_F(DeviceTest, CreateDestroyDeviceWithQueue) {
  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.queueFamilyIndex = 0;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo CreateInfo{};
  CreateInfo.queueCreateInfoCount = 1;
  CreateInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDevice(Physical, &CreateInfo, nullptr, &Device),
            VK_SUCCESS);
  ASSERT_NE(Device, VK_NULL_HANDLE);

  VkQueue Queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(Device, 0, 0, &Queue);
  EXPECT_NE(Queue, VK_NULL_HANDLE);

  // Only one queue was requested; a second index finds nothing.
  VkQueue Missing = reinterpret_cast<VkQueue>(1);
  vkGetDeviceQueue(Device, 0, 1, &Missing);
  EXPECT_EQ(Missing, VK_NULL_HANDLE);

  EXPECT_EQ(vkDeviceWaitIdle(Device), VK_SUCCESS);
  vkDestroyDevice(Device, nullptr);
}

TEST_F(DeviceTest, RejectsUnknownQueueFamily) {
  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  // Three families exist (index 0: universal, index 1: dedicated
  // transfer, index 2: dedicated compute; see roadmap C7), so the first
  // genuinely unknown index is 3.
  QueueInfo.queueFamilyIndex = 3;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo CreateInfo{};
  CreateInfo.queueCreateInfoCount = 1;
  CreateInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDevice(Physical, &CreateInfo, nullptr, &Device),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(DeviceTest, CreateDestroyDeviceWithDedicatedTransferQueue) {
  // Roadmap C7: family 1 is the dedicated transfer-only family, and must
  // be usable to create a device queue exactly like family 0.
  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.queueFamilyIndex = 1;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo CreateInfo{};
  CreateInfo.queueCreateInfoCount = 1;
  CreateInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDevice(Physical, &CreateInfo, nullptr, &Device),
            VK_SUCCESS);

  VkQueue Queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(Device, 1, 0, &Queue);
  EXPECT_NE(Queue, VK_NULL_HANDLE);

  vkDestroyDevice(Device, nullptr);
}

TEST_F(DeviceTest, CreateDestroyDeviceWithDedicatedComputeQueue) {
  // Roadmap C7: family 2 is the dedicated compute-only (excluding
  // graphics) family, and must be usable to create a device queue exactly
  // like families 0 and 1.
  float Priority = 1.0f;
  VkDeviceQueueCreateInfo QueueInfo{};
  QueueInfo.queueFamilyIndex = 2;
  QueueInfo.queueCount = 1;
  QueueInfo.pQueuePriorities = &Priority;

  VkDeviceCreateInfo CreateInfo{};
  CreateInfo.queueCreateInfoCount = 1;
  CreateInfo.pQueueCreateInfos = &QueueInfo;

  VkDevice Device = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDevice(Physical, &CreateInfo, nullptr, &Device),
            VK_SUCCESS);

  VkQueue Queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(Device, 2, 0, &Queue);
  EXPECT_NE(Queue, VK_NULL_HANDLE);

  vkDestroyDevice(Device, nullptr);
}

TEST_F(DeviceTest, RejectsUnknownExtension) {
  VkDeviceCreateInfo CreateInfo{};
  const char *Ext = "VK_KHR_not_implemented";
  CreateInfo.enabledExtensionCount = 1;
  CreateInfo.ppEnabledExtensionNames = &Ext;
  VkDevice Device = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDevice(Physical, &CreateInfo, nullptr, &Device),
            VK_ERROR_EXTENSION_NOT_PRESENT);
}

} // namespace
