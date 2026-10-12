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

// (Roadmap H46) `dataForRange` is the range-aware generalization of
// `data()`: for an ordinarily (fully) bound buffer, any in-bounds
// sub-range resolves to the expected offset into the same bound memory.
TEST_F(BufferTest, DataForRangeResolvesSubRangeOfOrdinaryBinding) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 128;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);

  VkMemoryRequirements Reqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);
  EXPECT_EQ(vkBindBufferMemory(Device, Buf, Memory, 0), VK_SUCCESS);

  Buffer *B = fromHandle<Buffer>(Buf);
  EXPECT_EQ(B->dataForRange(0, 128), B->data());
  EXPECT_EQ(B->dataForRange(32, 16),
           static_cast<uint8_t *>(B->data()) + 32);
  // Past the buffer's own declared size is still out of range even
  // though the underlying allocation (`Reqs.size`) may be larger.
  EXPECT_EQ(B->dataForRange(120, 16), nullptr);

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}


// `vkQueueBindSparse` would call for a (currently unreachable, since no
// live buffer here can have `VK_BUFFER_CREATE_SPARSE_BINDING_BIT`) sparse
// buffer bind; exercises it directly against a real `Buffer` object.
TEST_F(BufferTest, BindSparseRangeAccumulatesWithoutDisturbingData) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 128;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);

  VkMemoryRequirements Reqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory FirstMemory = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &FirstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &SecondMemory),
            VK_SUCCESS);

  EXPECT_EQ(vkBindBufferMemory(Device, Buf, FirstMemory, 0), VK_SUCCESS);
  void *ExpectedData = fromHandle<Buffer>(Buf)->data();

  fromHandle<Buffer>(Buf)->bindSparseRange(
      64, fromHandle<DeviceMemory>(SecondMemory), 0, 64);

  // The ordinary (bindWhole-produced) binding's own flat pointer is
  // unaffected by a later bindSparseRange call -- only `ranges()` grows.
  EXPECT_EQ(fromHandle<Buffer>(Buf)->data(), ExpectedData);
  ASSERT_EQ(fromHandle<Buffer>(Buf)->ranges().size(), 2u);
  EXPECT_EQ(fromHandle<Buffer>(Buf)->ranges()[1].ResourceOffset, 64u);
  EXPECT_EQ(fromHandle<Buffer>(Buf)->ranges()[1].Memory,
            fromHandle<DeviceMemory>(SecondMemory));

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, FirstMemory, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

// (Roadmap L416, phase 3) `reserveVirtualAddressSpace`/`virtualAddressBase`
// are only reachable by calling directly on the `Buffer` object (bypassing
// `vkCreateBuffer`'s own `isValidBufferCreateInfo` rejection of any
// `VkBufferCreateInfo::flags`), exactly like `BindSparseRangeAccumulates
// WithoutDisturbingData` above already does for `bindSparseRange` -- see
// `reserveVirtualAddressSpace`'s own comment (Buffer.h) for why no live
// buffer can reach this yet.
TEST_F(BufferTest, VirtualAddressReservationTracksBoundMemoryContent) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 64;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  Buffer *BufObj = fromHandle<Buffer>(Buf);
  ASSERT_TRUE(BufObj->reserveVirtualAddressSpace());
  ASSERT_TRUE(BufObj->hasVirtualAddressReservation());
  ASSERT_NE(BufObj->virtualAddressBase(), nullptr);

  VkMemoryRequirements Reqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Buf, Memory, 0), VK_SUCCESS);

  if (fromHandle<DeviceMemory>(Memory)->shareableFd() >= 0) {
    // A write through the ordinary bound-memory pointer must be visible
    // through the reservation's own base too: both are mappings of the
    // same underlying shareable pages (`VirtualAddressReservation::
    // mapRange`'s own doc comment).
    static_cast<uint8_t *>(BufObj->data())[0] = 0x42;
    EXPECT_EQ(static_cast<uint8_t *>(BufObj->virtualAddressBase())[0], 0x42);

    // `vkGetBufferDeviceAddress` must prefer the reservation's base over
    // `data()` once one exists (`vkGetBufferDeviceAddress`'s own comment,
    // Buffer.cpp).
    VkBufferDeviceAddressInfo AddrInfo{};
    AddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    AddrInfo.buffer = Buf;
    VkDeviceAddress Addr = vkGetBufferDeviceAddress(Device, &AddrInfo);
    EXPECT_EQ(reinterpret_cast<void *>(Addr), BufObj->virtualAddressBase());
    EXPECT_NE(reinterpret_cast<void *>(Addr), BufObj->data());
  }

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

TEST_F(BufferTest, VirtualAddressReservationTracksSparseRangeRebind) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 128;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  Buffer *BufObj = fromHandle<Buffer>(Buf);
  ASSERT_TRUE(BufObj->reserveVirtualAddressSpace());

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory FirstMemory = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &FirstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &SecondMemory),
            VK_SUCCESS);

  if (fromHandle<DeviceMemory>(FirstMemory)->shareableFd() >= 0 &&
      fromHandle<DeviceMemory>(SecondMemory)->shareableFd() >= 0) {
    static_cast<uint8_t *>(fromHandle<DeviceMemory>(FirstMemory)->data())[0] =
        0x11;
    static_cast<uint8_t *>(fromHandle<DeviceMemory>(SecondMemory)->data())[0] =
        0x22;

    BufObj->bindSparseRange(0, fromHandle<DeviceMemory>(FirstMemory), 0, 64);
    EXPECT_EQ(static_cast<uint8_t *>(BufObj->virtualAddressBase())[0], 0x11);

    // Rebinding the same resource-space range to different backing memory
    // must be reflected at the *same* reservation address -- the whole
    // point of a stable virtual-address reservation (this header's own
    // file comment, `VirtualAddressReservation.h`).
    BufObj->bindSparseRange(0, fromHandle<DeviceMemory>(SecondMemory), 0, 64);
    EXPECT_EQ(static_cast<uint8_t *>(BufObj->virtualAddressBase())[0], 0x22);

    // An explicit unbind (`Memory == nullptr`) must restore the no-access
    // gap, not leave the prior mapping's content silently reachable.
    BufObj->bindSparseRange(0, nullptr, 0, 64);
  }

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, FirstMemory, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}


// must resolve via vkGetDeviceProcAddr regardless of bufferDeviceAddress's
// (always-false) feature bit -- see vkGetBufferDeviceAddress's own comment
// (Buffer.cpp) for why both return the bound buffer's host pointer.
TEST_F(BufferTest, GetBufferDeviceAddressMatchesBoundHostPointer) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 64;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);

  VkMemoryRequirements Reqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = Reqs.size;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Buf, Memory, 0), VK_SUCCESS);

  VkBufferDeviceAddressInfo AddrInfo{};
  AddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
  AddrInfo.buffer = Buf;
  VkDeviceAddress Addr = vkGetBufferDeviceAddress(Device, &AddrInfo);
  EXPECT_EQ(reinterpret_cast<void *>(Addr), fromHandle<Buffer>(Buf)->data());
  EXPECT_EQ(vkGetBufferOpaqueCaptureAddress(Device, &AddrInfo), Addr);

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

// Roadmap L355: `VK_KHR_maintenance6`'s `VkBindMemoryStatusKHR`, chained
// onto a `VkBindBufferMemoryInfo`, must have its `pResult` written with the
// real per-binding result -- see `ImageTest.cpp`'s identical
// `BindImageMemory2WritesBindMemoryStatusResult` for the full rationale
// (CTS pre-seeds `pResult` with `VK_ERROR_UNKNOWN` to detect a driver that
// never touches it).
TEST_F(BufferTest, BindBufferMemory2WritesBindMemoryStatusResult) {
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

  VkResult BindResult = VK_ERROR_UNKNOWN;
  VkBindMemoryStatusKHR Status{};
  Status.sType = VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR;
  Status.pResult = &BindResult;
  VkBindBufferMemoryInfo BindInfo{};
  BindInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
  BindInfo.pNext = &Status;
  BindInfo.buffer = Buf;
  BindInfo.memory = Memory;
  BindInfo.memoryOffset = 0;
  ASSERT_EQ(vkBindBufferMemory2(Device, 1, &BindInfo), VK_SUCCESS);
  EXPECT_EQ(BindResult, VK_SUCCESS);

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

/// Roadmap E4 (`VK_KHR_maintenance4`): the same requirements a live
/// `VkBuffer` of this size/usage would report, computed from its
/// `VkBufferCreateInfo` alone -- no `vkCreateBuffer` call at all.
TEST_F(BufferTest, GetDeviceBufferMemoryRequirementsMatchesLiveBuffer) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 256;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  VkMemoryRequirements LiveReqs{};
  vkGetBufferMemoryRequirements(Device, Buf, &LiveReqs);
  vkDestroyBuffer(Device, Buf, nullptr);

  VkDeviceBufferMemoryRequirements Info{};
  Info.pCreateInfo = &BufferInfo;
  VkMemoryRequirements2 Reqs2{};
  vkGetDeviceBufferMemoryRequirements(Device, &Info, &Reqs2);
  EXPECT_EQ(Reqs2.memoryRequirements.size, LiveReqs.size);
  EXPECT_EQ(Reqs2.memoryRequirements.alignment, LiveReqs.alignment);
  EXPECT_EQ(Reqs2.memoryRequirements.memoryTypeBits, LiveReqs.memoryTypeBits);
  EXPECT_EQ(Reqs2.memoryRequirements.size, 256u);
}

/// Roadmap E4: a chained `VkMemoryDedicatedRequirements` must report the
/// identical answer from `vkGetBufferMemoryRequirements2` (a live
/// `VkBuffer`) and `vkGetDeviceBufferMemoryRequirements` (a
/// `VkBufferCreateInfo` alone) -- this ICD never requires or prefers a
/// dedicated allocation (see "Memory and Buffers"), so both must report
/// `VK_FALSE` for each field, not merely agree by coincidence.
TEST_F(BufferTest, DedicatedRequirementsAgreeBetweenLiveAndDeviceEntrypoints) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 64;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  VkBufferMemoryRequirementsInfo2 LiveInfo{};
  LiveInfo.buffer = Buf;
  VkMemoryDedicatedRequirements LiveDedicated{};
  LiveDedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  VkMemoryRequirements2 LiveReqs2{};
  LiveReqs2.pNext = &LiveDedicated;
  vkGetBufferMemoryRequirements2(Device, &LiveInfo, &LiveReqs2);
  vkDestroyBuffer(Device, Buf, nullptr);

  VkDeviceBufferMemoryRequirements DeviceInfo{};
  DeviceInfo.pCreateInfo = &BufferInfo;
  VkMemoryDedicatedRequirements DeviceDedicated{};
  DeviceDedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  VkMemoryRequirements2 DeviceReqs2{};
  DeviceReqs2.pNext = &DeviceDedicated;
  vkGetDeviceBufferMemoryRequirements(Device, &DeviceInfo, &DeviceReqs2);

  EXPECT_EQ(LiveDedicated.prefersDedicatedAllocation, VK_FALSE);
  EXPECT_EQ(LiveDedicated.requiresDedicatedAllocation, VK_FALSE);
  EXPECT_EQ(DeviceDedicated.prefersDedicatedAllocation,
            LiveDedicated.prefersDedicatedAllocation);
  EXPECT_EQ(DeviceDedicated.requiresDedicatedAllocation,
            LiveDedicated.requiresDedicatedAllocation);
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
        VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R8G8B8A8_UNORM,
        // (Roadmap L9) The single-channel identity formats, e.g.
        // `RWBuffer<float>`/`RWBuffer<int>`/`RWBuffer<uint>`'s own shape.
        VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT}) {
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
  // (Roadmap H8d) `R16G16B16A16_SFLOAT` moved into
  // `isTexelBufferFormatSupported`'s scope this row -- `R32G32B32_SFLOAT`
  // (a 3-component format, never a legal texel-buffer shape since no
  // SPIR-V/GLSL image type returns a `<3 x T>`) stays outside every
  // format-table scope this project defines, so it remains a reliable
  // "no runtime conversion" negative case.
  ViewInfo.format = VK_FORMAT_R32G32B32_SFLOAT;
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
