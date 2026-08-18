//===- Buffer.cpp - VkBuffer implementations ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Buffer.h"
#include "Icd.h"
#include "Objects.h"

using namespace feme::vulkan;

namespace {

/// Fills \p Reqs for \p Buf, per "Memory and Buffers": alignment tracks the
/// real range-check granularity this ICD enforces, and every allocation is
/// eligible (only memory type 0 exists).
void getRequirements(const Buffer &Buf, const PhysicalDeviceInfo &Info,
                     VkMemoryRequirements &Reqs) {
  Reqs.size = Buf.size();
  Reqs.alignment = Info.Properties.limits.minStorageBufferOffsetAlignment;
  Reqs.memoryTypeBits = 0x1;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
  (void)device;
  // No sparse binding is supported (see "Initial Non-Goals").
  if (pCreateInfo->flags != 0)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (pCreateInfo->size == 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  Buffer *Obj = Alloc.create<Buffer>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                     pCreateInfo->size, pCreateInfo->usage);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pBuffer = toHandle<VkBuffer>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
  if (!buffer)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Buffer>(buffer));
}

VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                              VkMemoryRequirements *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  getRequirements(*fromHandle<Buffer>(buffer), Info, *pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  getRequirements(*fromHandle<Buffer>(pInfo->buffer), Info,
                  pMemoryRequirements->memoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice, VkBuffer buffer,
                                                  VkDeviceMemory memory,
                                                  VkDeviceSize memoryOffset) {
  fromHandle<Buffer>(buffer)->bind(fromHandle<DeviceMemory>(memory),
                                   memoryOffset);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                    const VkBindBufferMemoryInfo *pBindInfos) {
  for (uint32_t I = 0; I != bindInfoCount; ++I)
    feme::vulkan::vkBindBufferMemory(device, pBindInfos[I].buffer,
                                     pBindInfos[I].memory,
                                     pBindInfos[I].memoryOffset);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice, const VkBufferViewCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkBufferView *pView) {
  std::optional<feme::cpu::ResourceFormat> Format =
      mapVkFormat(pCreateInfo->format);
  if (!Format)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;

  auto *Buf = fromHandle<Buffer>(pCreateInfo->buffer);
  if (!Buf->isBound())
    return VK_ERROR_INITIALIZATION_FAILED;
  VkDeviceSize Range = pCreateInfo->range == VK_WHOLE_SIZE
                           ? Buf->size() - pCreateInfo->offset
                           : pCreateInfo->range;
  if (pCreateInfo->offset > Buf->size() ||
      Range > Buf->size() - pCreateInfo->offset)
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  vulkan::BufferView *Obj =
      Alloc.create<vulkan::BufferView>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, Buf,
                                       *Format, pCreateInfo->offset, Range);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pView = toHandle<VkBufferView>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
  if (!bufferView)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<vulkan::BufferView>(bufferView));
}

} // namespace feme::vulkan
