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

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_MEMORY_H
