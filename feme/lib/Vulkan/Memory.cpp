//===- Memory.cpp - VkDeviceMemory implementations ----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Memory.h"
#include "Icd.h"
#include "Objects.h"

#include <cstdlib>

using namespace feme::vulkan;

namespace {

/// Allocates \p Size bytes aligned to \p Alignment, the device's own
/// backing-store allocation for a `VkDeviceMemory` (distinct from the ICD
/// object's own `VkAllocationCallbacks`-governed allocation -- see "Memory
/// and Buffers": device memory is host RAM the driver owns directly).
/// Returns null on failure, matching every other allocation path here.
void *allocateAligned(size_t Size, size_t Alignment) {
  void *Ptr = nullptr;
  if (posix_memalign(&Ptr,
                     Alignment < sizeof(void *) ? sizeof(void *) : Alignment,
                     Size) != 0)
    return nullptr;
  return Ptr;
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
    const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) {
  Device *Dev = fromHandle<Device>(device);
  const PhysicalDeviceInfo &Info = Dev->getPhysicalDevice().getInfo();

  // Only one memory type (index 0) is ever reported (see "Memory and
  // Buffers"): "one memory type and one heap".
  if (pAllocateInfo->memoryTypeIndex != 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  size_t Alignment = Info.Properties.limits.minMemoryMapAlignment;
  void *Data = allocateAligned(
      static_cast<size_t>(pAllocateInfo->allocationSize), Alignment);
  if (!Data)
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;

  Allocator Alloc(pAllocator);
  DeviceMemory *Obj = Alloc.create<DeviceMemory>(
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, Data, pAllocateInfo->allocationSize);
  if (!Obj) {
    std::free(Data);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  *pMemory = toHandle<VkDeviceMemory>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) {
  if (!memory)
    return;
  DeviceMemory *Obj = fromHandle<DeviceMemory>(memory);
  std::free(Obj->data());
  Allocator Alloc(pAllocator);
  Alloc.destroy(Obj);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice, VkDeviceMemory memory,
                                           VkDeviceSize offset,
                                           VkDeviceSize size, VkMemoryMapFlags,
                                           void **ppData) {
  DeviceMemory *Obj = fromHandle<DeviceMemory>(memory);
  // `VK_WHOLE_SIZE` maps from `offset` to the end of the allocation.
  VkDeviceSize MapSize = size == VK_WHOLE_SIZE ? Obj->size() - offset : size;
  if (offset > Obj->size() || MapSize > Obj->size() - offset)
    return VK_ERROR_MEMORY_MAP_FAILED;
  *ppData = static_cast<uint8_t *>(Obj->data()) + offset;
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice, VkDeviceMemory) {
  // Coherent memory needs no unmap-time bookkeeping (see "Memory and
  // Buffers": "Coherent memory avoids cache-management work").
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(
    VkDevice device, const VkMemoryMapInfo *pMemoryMapInfo, void **ppData) {
  // `VkMemoryMapInfo`'s `pNext` chain has no recognized extension struct
  // (roadmap F14 -- `VK_KHR_map_memory2`'s only other user, `VK_EXT_map_
  // memory_placed`, is not implemented/advertised), so every field this
  // wrapper needs comes straight off the struct itself, matching
  // `vkBindBufferMemory2`'s (Buffer.cpp) "unwrap the info struct, call the
  // plain form" precedent.
  return feme::vulkan::vkMapMemory(device, pMemoryMapInfo->memory,
                                   pMemoryMapInfo->offset,
                                   pMemoryMapInfo->size,
                                   pMemoryMapInfo->flags, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo *pMemoryUnmapInfo) {
  // `VK_MEMORY_UNMAP_RESERVE_BIT_EXT` only has meaning together with
  // `VK_EXT_map_memory_placed`'s reservation-backed mapping, which this
  // ICD does not implement/advertise: a real placed mapping never exists
  // here to reserve, so the bit is validate-only, exactly like the rest
  // of `vkUnmapMemory`'s coherent-memory no-op above (see "Memory and
  // Buffers"). `VkMemoryUnmapInfo`'s `pNext` chain has no recognized
  // extension struct either, for the same reason as `vkMapMemory2` above.
  feme::vulkan::vkUnmapMemory(device, pMemoryUnmapInfo->memory);
  return VK_SUCCESS;
}

// `vkMapMemory2`/`vkUnmapMemory2` above are already core, non-`KHR`-suffixed
// `VK_VERSION_1_4` entries `vk_gen_entrypoints.py`'s `CORE_FEATURES`
// resolves, but (like `VK_KHR_maintenance5`'s granularity/subresource-layout
// commands) a `deqp-vk` test whose own negotiated `usedApiVersion` is below
// 1.4 falls back to the `KHR` name instead, which the loader's icd.json
// `api_version` (1.1) makes otherwise unreachable -- see
// `vk_gen_entrypoints.py`'s `SUPPORTED_EXTENSIONS` comment for this row.
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2KHR(
    VkDevice device, const VkMemoryMapInfoKHR *pMemoryMapInfo, void **ppData) {
  return feme::vulkan::vkMapMemory2(device, pMemoryMapInfo, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2KHR(
    VkDevice device, const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo) {
  return feme::vulkan::vkUnmapMemory2(device, pMemoryUnmapInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkFlushMappedMemoryRanges(VkDevice, uint32_t memoryRangeCount,
                          const VkMappedMemoryRange *pMemoryRanges) {
  // Every reported memory type is `HOST_COHERENT`, so flush/invalidate only
  // validate ranges and otherwise do nothing (see "Memory and Buffers").
  for (uint32_t I = 0; I != memoryRangeCount; ++I) {
    DeviceMemory *Obj = fromHandle<DeviceMemory>(pMemoryRanges[I].memory);
    VkDeviceSize Size = pMemoryRanges[I].size == VK_WHOLE_SIZE
                            ? Obj->size() - pMemoryRanges[I].offset
                            : pMemoryRanges[I].size;
    if (pMemoryRanges[I].offset > Obj->size() ||
        Size > Obj->size() - pMemoryRanges[I].offset)
      return VK_ERROR_MEMORY_MAP_FAILED;
  }
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                               const VkMappedMemoryRange *pMemoryRanges) {
  return feme::vulkan::vkFlushMappedMemoryRanges(device, memoryRangeCount,
                                                 pMemoryRanges);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes) {
  // Not lazily-allocated (no `LAZILY_ALLOCATED` memory type is reported), so
  // the whole allocation is always committed.
  *pCommittedMemoryInBytes = fromHandle<DeviceMemory>(memory)->size();
}

void fillMemoryRequirements2PNextChain(void *PNext) {
  for (auto *Base = static_cast<VkBaseOutStructure *>(PNext); Base;
       Base = Base->pNext) {
    if (Base->sType != VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)
      continue;
    // No dedicated allocation of any kind is ever required or preferred:
    // every `VkBuffer`/`VkImage` binds into a plain suballocation of a
    // `VkDeviceMemory` the same way (see "Memory and Buffers"), with no
    // real hardware dedicated-resource requirement to report honestly as
    // `VK_TRUE` for. `Buffer.cpp`'s `vkGetBufferMemoryRequirements2`/
    // `vkGetDeviceBufferMemoryRequirements` and `Image.cpp`'s
    // `vkGetImageMemoryRequirements2`/`vkGetDeviceImageMemoryRequirements`
    // (roadmap E4) all share this one chain-walk so every one of the four
    // reports the identical, consistent answer for the same
    // `VkMemoryDedicatedRequirements` pNext struct.
    auto *Dedicated = reinterpret_cast<VkMemoryDedicatedRequirements *>(Base);
    Dedicated->prefersDedicatedAllocation = VK_FALSE;
    Dedicated->requiresDedicatedAllocation = VK_FALSE;
  }
}

} // namespace feme::vulkan
