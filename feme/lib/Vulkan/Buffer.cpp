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

/// Returns whether \p CreateInfo describes a `VkBuffer` this ICD can
/// create, per "Initial Non-Goals" (no sparse binding) -- shared by
/// `vkCreateBuffer` and, roadmap E4's `VK_KHR_maintenance4`
/// `vkGetDeviceBufferMemoryRequirements`, which must reject the same
/// unsupported shapes before computing anything from a `VkBufferCreateInfo`
/// alone, with no live `VkBuffer` to fall back on.
bool isValidBufferCreateInfo(const VkBufferCreateInfo &CreateInfo) {
  if (CreateInfo.flags != 0)
    return false;
  if (CreateInfo.size == 0)
    return false;
  return true;
}

/// Fills \p Reqs for a buffer of \p Size, per "Memory and Buffers":
/// alignment tracks the real range-check granularity this ICD enforces, and
/// every allocation is eligible (only memory type 0 exists). Shared by the
/// live `vkGetBufferMemoryRequirements(2)` entrypoints (an already-created
/// `VkBuffer`'s own size) and roadmap E4's info-only
/// `vkGetDeviceBufferMemoryRequirements` (a `VkBufferCreateInfo`'s `size`
/// field, with no `VkBuffer` ever created).
void computeBufferMemoryRequirements(VkDeviceSize Size,
                                     const PhysicalDeviceInfo &Info,
                                     VkMemoryRequirements &Reqs) {
  Reqs.size = Size;
  Reqs.alignment = Info.Properties.limits.minStorageBufferOffsetAlignment;
  Reqs.memoryTypeBits = 0x1;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
  (void)device;
  if (!isValidBufferCreateInfo(*pCreateInfo))
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
  computeBufferMemoryRequirements(fromHandle<Buffer>(buffer)->size(), Info,
                                  *pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  computeBufferMemoryRequirements(fromHandle<Buffer>(pInfo->buffer)->size(),
                                  Info, pMemoryRequirements->memoryRequirements);
}

/// (roadmap E4) `VK_KHR_maintenance4`: computes a `VkBuffer`'s memory
/// requirements from its `VkBufferCreateInfo` alone, without ever creating
/// the buffer -- shares `computeBufferMemoryRequirements` with the live
/// `vkGetBufferMemoryRequirements(2)` entrypoints above, the same way
/// `vkCreateBuffer` shares `isValidBufferCreateInfo`'s validation with this
/// entrypoint. Per the Vulkan specification, \p pInfo->pCreateInfo need not
/// describe a buffer this ICD could actually create for the result to be
/// meaningful, but this ICD's own requirements depend only on `size`
/// (`minStorageBufferOffsetAlignment`/memory-type-bits are constant), so no
/// further validation is needed here.
VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(
    VkDevice device, const VkDeviceBufferMemoryRequirements *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements) {
  const PhysicalDeviceInfo &Info =
      fromHandle<Device>(device)->getPhysicalDevice().getInfo();
  computeBufferMemoryRequirements(pInfo->pCreateInfo->size, Info,
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
  // A `VkBufferView` only ever backs a texel buffer (Vulkan has no other use
  // for one), so this is always the texel-buffer format check, not merely
  // "is this format known at all" -- see `isTexelBufferFormatSupported`'s
  // comment and Descriptor.h's file comment for why the two differ.
  if (!Format || !isTexelBufferFormatSupported(*Format))
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
