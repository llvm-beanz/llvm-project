//===- MemoryBinding.h - Resource-to-memory binding model -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap H46) A general resource-to-memory binding model: a `VkBuffer`
// or `VkImage`'s resource-space byte range can, in general, be covered by
// zero or more disjoint `MemoryRangeBinding`s, each mapping a contiguous
// resource-space sub-range onto a contiguous sub-range of some
// `VkDeviceMemory` allocation -- the shape a real sparse (partially
// resident) resource needs.
//
// Today, every bindable resource in this ICD is either fully bound by
// exactly one such range (covering its entire size, from
// `vkBind{Buffer,Image}Memory`) or not bound at all: no live resource can
// ever carry a sparse-residency creation flag (`isValidImageShape`,
// Image.cpp, already rejects it at `vkCreateImage` time), so
// `ResourceBindings::bindWhole` below is this model's only producer. This
// header exists so `Buffer`/`Image` can migrate their own internal state
// onto the general model now, with zero behavior change for the
// always-fully-bound-or-unbound case, ahead of H46 actually implementing
// real sparse (partial, possibly-gapped) binding via a second producer
// (e.g. a `bindRange` sibling of `bindWhole`, driven by `vkQueueBindSparse`
// once it stops ignoring its bind arrays -- see that function's own H46
// note in Sync.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_MEMORYBINDING_H
#define FEME_LIB_VULKAN_MEMORYBINDING_H

#include "Memory.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace feme::vulkan {

/// One bound range of a (potentially sparse) resource: resource-space
/// `[ResourceOffset, ResourceOffset + Size)` is backed by
/// `[MemoryOffset, MemoryOffset + Size)` of `Memory`'s own allocation.
struct MemoryRangeBinding {
  VkDeviceSize ResourceOffset;
  DeviceMemory *Memory;
  VkDeviceSize MemoryOffset;
  VkDeviceSize Size;
};

/// Tracks the set of memory ranges currently bound to one resource (a
/// `VkBuffer` or `VkImage`). See this header's own file comment for why
/// every instance today is only ever empty or a single whole-resource
/// range.
class ResourceBindings {
public:
  /// Binds `[0, Size)` of the resource to `[MemoryOffset, MemoryOffset +
  /// Size)` of `Memory`, replacing any existing binding. This is the
  /// ordinary (non-sparse) `vkBind{Buffer,Image}Memory` binding shape:
  /// exactly one range, covering the resource's entire declared size.
  /// Per Vulkan's binding rules, a real application calls this at most
  /// once per resource (rebinding a bound resource is invalid usage this
  /// ICD does not re-validate), matching `Buffer`/`Image`'s own pre-H46
  /// `bind()` contract this replaces.
  void bindWhole(DeviceMemory *Memory, VkDeviceSize MemoryOffset,
                 VkDeviceSize Size) {
    Ranges.clear();
    Ranges.push_back({0, Memory, MemoryOffset, Size});
  }

  bool isBound() const { return !Ranges.empty(); }

  /// The resource's flat base data pointer: valid only in the
  /// always-true-today case of zero or one bound range starting at
  /// resource offset 0 (the only shape `bindWhole` can produce). Null if
  /// unbound. A future sparse-aware caller needing a non-flat resource's
  /// data must use `ranges()` directly instead once more than one range
  /// (or a range not starting at offset 0) becomes possible.
  void *data() const {
    if (Ranges.empty())
      return nullptr;
    const MemoryRangeBinding &R = Ranges.front();
    return static_cast<uint8_t *>(R.Memory->data()) + R.MemoryOffset;
  }

  llvm::ArrayRef<MemoryRangeBinding> ranges() const { return Ranges; }

private:
  /// `SmallVector<..., 1>`: every instance today holds exactly 0 or 1
  /// ranges, so the common case needs no heap allocation at all; a real
  /// sparse resource (post-H46) may grow this past 1.
  llvm::SmallVector<MemoryRangeBinding, 1> Ranges;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_MEMORYBINDING_H
