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

#include "ExecutionLifetime.h"
#include "Format.h"
#include "Memory.h"
#include "MemoryBinding.h"
#include "VirtualAddressReservation.h"

#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace feme::vulkan {

/// A `VkBuffer`. Not dispatchable.
///
/// (Roadmap L410) Inherits `PendingExecutionRefCounted` (see its own file
/// comment, `ExecutionLifetime.h`) for the same reason `Pipeline`/
/// `RenderPass`/`Framebuffer` do (`L409`): `RecordedCommand` captures every
/// `Buffer *` it references (`IndirectBuffer`/`CountBuffer`/`SrcBuffer`/
/// `DstBuffer`/`VertexBuffers`/`XfbBuffers`, `CommandBuffer.h`) as a raw
/// pointer with no keep-alive, so a `vkDestroyBuffer` call made right
/// after `vkQueueSubmit` returns, with no wait at all, can otherwise race
/// a still-pending `QueueExecutor` task's own read of it.
class Buffer : public PendingExecutionRefCounted {
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
    Bindings.bindWhole(Memory, Offset, Size);
    if (VAReservation.isReserved() && Memory && Memory->shareableFd() >= 0)
      VAReservation.mapRange(0, Size, Memory->shareableFd(), Offset);
  }

  bool isBound() const { return Bindings.isBound(); }

  /// (Roadmap L416, phase 3) Reserves `size()` bytes of virtual address
  /// space for this buffer -- see "Memory and Buffers" in
  /// `FeMeVulkanDesign.md` for the full design. No live `VkBuffer` calls
  /// this today (`isValidBufferCreateInfo`, Buffer.cpp, still rejects any
  /// nonzero `VkBufferCreateInfo::flags`, so `VK_BUFFER_CREATE_SPARSE_
  /// BINDING_BIT` can never reach a real `Buffer`), matching
  /// `bindSparseRange`'s own "exists for internals/tests to call
  /// honestly" precedent. Returns false on failure (e.g. platform support,
  /// `VirtualAddressReservation::reserve`'s own doc comment).
  bool reserveVirtualAddressSpace() { return VAReservation.reserve(Size); }

  /// True once `reserveVirtualAddressSpace` has succeeded.
  bool hasVirtualAddressReservation() const {
    return VAReservation.isReserved();
  }

  /// The reservation's base address, stable across any later `bind`/
  /// `bindSparseRange` call -- see `vkGetBufferDeviceAddress`'s own
  /// comment (Buffer.cpp) for why this, not `data()`, is the address a
  /// future genuinely-sparse buffer must hand to the application.
  /// Null if `hasVirtualAddressReservation()` is false.
  void *virtualAddressBase() const { return VAReservation.base(); }

  /// (Roadmap H46) Records one `VkSparseMemoryBind` from
  /// `vkQueueBindSparse`'s `pBufferBinds`: binds resource-space
  /// `[ResourceOffset, ResourceOffset + Size)` to `[MemoryOffset,
  /// MemoryOffset + Size)` of `Memory` (or unbinds that range if
  /// `Memory` is null), additively -- see `ResourceBindings::bindRange`'s
  /// own comment. No live buffer in this ICD can require this today
  /// (`isValidBufferCreateInfo`, Buffer.cpp, already rejects
  /// `VK_BUFFER_CREATE_SPARSE_BINDING_BIT` at `vkCreateBuffer` time), so
  /// this exists for `vkQueueBindSparse` to call honestly rather than
  /// silently drop a (currently unreachable, per spec, without
  /// `sparseBinding`) bind array entry.
  void bindSparseRange(VkDeviceSize ResourceOffset, DeviceMemory *Memory,
                       VkDeviceSize MemoryOffset, VkDeviceSize Size) {
    Bindings.bindRange(ResourceOffset, Memory, MemoryOffset, Size);
    // (Roadmap L416, phase 3) Mirror the same range into the virtual
    // address reservation, if one exists, so a device address obtained
    // via `virtualAddressBase()` stays valid (and faults safely on an
    // unbound gap) across a later rebind -- see this header's own
    // `reserveVirtualAddressSpace` comment for why no live buffer can
    // reach this yet. A `Memory == nullptr` bind is spec's own explicit
    // "unbind this range" (`ResourceBindings::bindRange`'s own comment);
    // a real bind with no shareable fd (e.g. a non-`memfd`-backed
    // fallback allocation) cannot be remapped into the reservation at
    // all, so it is left unmapped rather than silently misrepresented.
    if (VAReservation.isReserved()) {
      if (!Memory)
        VAReservation.unmapRange(ResourceOffset, Size);
      else if (Memory->shareableFd() >= 0)
        VAReservation.mapRange(ResourceOffset, Size, Memory->shareableFd(),
                               MemoryOffset);
    }
  }

  /// The buffer's full set of bound ranges -- see `ResourceBindings::
  /// ranges()`'s own comment for why every instance today holds 0 or 1
  /// entries, growing past 1 only via `bindSparseRange`.
  llvm::ArrayRef<MemoryRangeBinding> ranges() const {
    return Bindings.ranges();
  }

  /// The buffer's data pointer: its bound memory's base plus the binding
  /// offset (see "Memory and Buffers": "Data = memory allocation base +
  /// buffer binding offset + descriptor offset"). Null if unbound.
  void *data() const { return Bindings.data(); }

  /// (Roadmap H46) The range-aware generalization of `data()`: resolves a
  /// flat pointer for buffer-space sub-range `[Offset, Offset + Size)`,
  /// or `nullptr` if that sub-range is not fully covered by one bound
  /// range -- see `ResourceBindings::dataForRange`'s own comment. Prefer
  /// this over `data() + Offset` for any byte-range-shaped buffer access
  /// (a copy/fill/update region, a query-result write, etc.).
  void *dataForRange(VkDeviceSize Offset, VkDeviceSize Size) const {
    return Bindings.dataForRange(Offset, Size);
  }

private:
  VkDeviceSize Size;
  VkBufferUsageFlags Usage;
  /// (Roadmap H46) Backed by the general sparse-capable binding model;
  /// see MemoryBinding.h's file comment for why this is only ever empty
  /// or a single whole-buffer range today.
  ResourceBindings Bindings;
  /// (Roadmap L416, phase 3) Unreserved (`isReserved() == false`) for
  /// every live buffer today -- see `reserveVirtualAddressSpace`'s own
  /// comment above.
  VirtualAddressReservation VAReservation;
};

/// A `VkBufferView`: a typed window into a `VkBuffer`, consumed only by a
/// uniform/storage texel buffer descriptor (see "Descriptor Model" in
/// feme/docs/FeMeVulkanDesign.md's texel-buffer rows, and Descriptor.h's
/// file comment for exactly which formats V4 supports). Not dispatchable.
///
/// (roadmap L410) Inherits `PendingExecutionRefCounted` (`ExecutionLifetime.h`)
/// since a `BufferView` can be bound into a `DescriptorSet`'s texel-buffer
/// binding, and a submission referencing that descriptor set may still be
/// executing on a `QueueExecutor` worker thread when `vkDestroyBufferView`
/// is called -- the same raw-pointer-capture-with-no-keep-alive race
/// `L409`/`L410` fixed for directly-recorded resource types, reached here
/// only indirectly through `DescriptorSet`'s own retain sweep
/// (`Sync.cpp`'s `collectDescriptorSetRefs`).
class BufferView : public PendingExecutionRefCounted {
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
