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
// binding model. (V4) `VkBufferView`: a typed window into a `VkBuffer`,
// consumed by a uniform/storage texel buffer descriptor.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_BUFFER_H
#define FEME_LIB_VULKAN_BUFFER_H

#include "Format.h"
#include "Memory.h"

#include "feme/Target/CPU/RuntimeABI.h"

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

/// A `VkBufferView`: a typed window into a `VkBuffer`, consumed only by a
/// uniform/storage texel buffer descriptor (see "Descriptor Model" in
/// feme/docs/FeMeVulkanDesign.md's texel-buffer rows, and Descriptor.h's
/// file comment for exactly which formats V4 supports). Not dispatchable.
class BufferView {
public:
  BufferView(Buffer *Buf, feme::cpu::ResourceFormat Format, VkDeviceSize Offset,
             VkDeviceSize Range)
      : Buf(Buf), Format(Format), Offset(Offset), Range(Range) {}

  Buffer *buffer() const { return Buf; }
  feme::cpu::ResourceFormat format() const { return Format; }
  VkDeviceSize offset() const { return Offset; }

  /// The view's byte range, with `VK_WHOLE_SIZE` already resolved against
  /// the bound buffer's size at creation time (Vulkan requires the buffer
  /// to already be bound when a `VkBufferView` over it is created).
  VkDeviceSize range() const { return Range; }

private:
  Buffer *Buf;
  feme::cpu::ResourceFormat Format;
  VkDeviceSize Offset;
  VkDeviceSize Range;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_BUFFER_H
