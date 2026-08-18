//===- Buffer.h - VkBuffer object model ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V1 `VkBuffer` object (see "Object Model" and "Memory and Buffers" in
// feme/docs/FeMeVulkanDesign.md): a size/usage record plus a bound memory
// range, bound in a separate `vkBindBufferMemory` call per the Vulkan
// binding model.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BUFFER_H
#define FEME_LIB_VULKAN_BUFFER_H

#include "Memory.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace feme::vulkan {

/// A `VkBuffer`. Not dispatchable.
class Buffer {
public:
  Buffer(VkDeviceSize Size, VkBufferUsageFlags Usage)
      : Size(Size), Usage(Usage) {}

  VkDeviceSize size() const { return Size; }
  VkBufferUsageFlags usage() const { return Usage; }

  /// Records the `(VkDeviceMemory, offset)` pair `vkBindBufferMemory` binds
  /// this buffer to. Must be called exactly once, per Vulkan's binding
  /// rules (rebinding a bound buffer is invalid usage the application must
  /// avoid; this ICD does not re-validate it).
  void bind(DeviceMemory *Memory, VkDeviceSize Offset) {
    BoundMemory = Memory;
    BoundOffset = Offset;
  }

  bool isBound() const { return BoundMemory != nullptr; }

  /// The buffer's data pointer: its bound memory's base plus the binding
  /// offset (see "Memory and Buffers": "Data = memory allocation base +
  /// buffer binding offset + descriptor offset"). Null if unbound.
  void *data() const {
    if (!BoundMemory)
      return nullptr;
    return static_cast<uint8_t *>(BoundMemory->data()) + BoundOffset;
  }

private:
  VkDeviceSize Size;
  VkBufferUsageFlags Usage;
  DeviceMemory *BoundMemory = nullptr;
  VkDeviceSize BoundOffset = 0;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BUFFER_H
