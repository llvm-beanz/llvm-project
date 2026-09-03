//===- Memory.h - VkDeviceMemory object model --------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V1 `VkDeviceMemory` object (see "Memory and Buffers" in
// feme/docs/FeMeVulkanDesign.md): a host allocation, aligned to at least
// `minMemoryMapAlignment`, that is always `HOST_VISIBLE | HOST_COHERENT |
// DEVICE_LOCAL` -- the physical device's one memory type -- so mapping is
// unconditional and flush/invalidate are validate-only no-ops.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_MEMORY_H
#define FEME_LIB_VULKAN_MEMORY_H

#include "Icd.h"

#include <cstddef>
#include <cstdint>

namespace feme::vulkan {

/// A `VkDeviceMemory`: one host allocation and its current map state. Not
/// dispatchable (Vulkan non-dispatchable handles need no loader dispatch
/// header), so this does not derive from `DispatchableBase`.
class DeviceMemory {
public:
  DeviceMemory(void *Data, VkDeviceSize Size) : Data(Data), Size(Size) {}

  void *data() const { return Data; }
  VkDeviceSize size() const { return Size; }

private:
  void *Data;
  VkDeviceSize Size;
};

/// Allocates \p Size bytes aligned to \p Alignment, the same host
/// allocation `vkAllocateMemory` itself performs for a `VkDeviceMemory`'s
/// backing store (distinct from an ICD object's own
/// `VkAllocationCallbacks`-governed allocation -- see "Memory and Buffers":
/// device memory is host RAM the driver owns directly). Returns null on
/// failure, matching every other allocation path here. Exposed (rather than
/// kept `static` inside Memory.cpp) so Swapchain.cpp's swapchain-image
/// backing store -- memory a real application never separately allocates
/// or binds itself, unlike an application-created `VkImage` -- can reuse
/// the identical alignment logic instead of a second, independently
/// maintained copy.
void *allocateDeviceMemory(size_t Size, size_t Alignment);

/// Walks a `VkMemoryRequirements2`-family `pNext` chain (also chained by
/// `VkDeviceBufferMemoryRequirements`/`VkDeviceImageMemoryRequirements`'
/// own `VkMemoryRequirements2` output), filling every recognized extension
/// struct honestly. Shared by Buffer.cpp's and Image.cpp's four
/// `vkGet*MemoryRequirements(2)`/`vkGetDevice*MemoryRequirements` (roadmap
/// E4) entrypoints, so all four report the identical answer for the same
/// chained struct.
void fillMemoryRequirements2PNextChain(void *PNext);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_MEMORY_H
