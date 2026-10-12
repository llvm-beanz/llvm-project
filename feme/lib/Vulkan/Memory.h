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
// (Roadmap L416 phase 2) An application-visible `VkDeviceMemory` (the
// `vkAllocateMemory`/`vkFreeMemory` path in Memory.cpp, as opposed to
// Swapchain.cpp's own internal, never-application-bound backing storage,
// which Vulkan disallows ever being sparse-bound and so is left on the
// original plain-heap allocator) is backed, on POSIX, by a shareable
// memory object (`VirtualAddressReservation.h`'s `createShareableMemoryFd`)
// rather than a plain `posix_memalign` allocation, so a future
// `vkQueueBindSparse` can later re-map the same physical pages into a
// buffer's reserved virtual-address window (see that header's own file
// comment, and "Memory and Buffers"'s `vkGetBufferDeviceAddress` section)
// without physically moving them. This changes nothing observable yet --
// `vkMapMemory`/`vkUnmapMemory`/flush/invalidate all still see one flat,
// stable host pointer, exactly as before -- it only prepares the
// allocation to be remappable once a future session wires up phases 3-4.
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
  /// \p ShareableFd is the handle backing `Data` if it came from
  /// `allocateShareableDeviceMemory` below (-1 otherwise, e.g. for
  /// Swapchain.cpp's internal backing storage, or on a platform where
  /// `createShareableMemoryFd` is not implemented) -- a future
  /// `vkQueueBindSparse` needs this to actually remap pages; nothing
  /// reads it yet.
  DeviceMemory(void *Data, VkDeviceSize Size, intptr_t ShareableFd = -1)
      : Data(Data), Size(Size), ShareableFd(ShareableFd) {}

  void *data() const { return Data; }
  VkDeviceSize size() const { return Size; }
  intptr_t shareableFd() const { return ShareableFd; }

private:
  void *Data;
  VkDeviceSize Size;
  intptr_t ShareableFd;
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

/// Frees storage returned by `allocateDeviceMemory`.
void freeDeviceMemory(void *Ptr);

/// (Roadmap L416 phase 2) Allocates \p Size bytes for an application-visible
/// `VkDeviceMemory` (`vkAllocateMemory`'s own backing store), backed by a
/// shareable memory object rather than a plain heap allocation, so a
/// future `vkQueueBindSparse` can remap the same physical pages into a
/// sparse buffer's reserved virtual-address window (see Memory.h's own
/// file comment). On success, `*OutFd` receives the shareable handle
/// (`DeviceMemory`'s `shareableFd()`); the returned pointer is always at
/// least page-aligned, satisfying any `Alignment` this driver currently
/// reports (`minMemoryMapAlignment` is 64 -- see PhysicalDeviceInfo.cpp --
/// well under the host page size), so unlike `allocateDeviceMemory` above,
/// \p Alignment is only ever asserted, not threaded into the underlying
/// allocation call.
///
/// On a platform where `createShareableMemoryFd` is not implemented (see
/// VirtualAddressReservation.h), falls back to `allocateDeviceMemory`
/// above unchanged, with `*OutFd` set to -1: this keeps every such
/// platform's existing, already-working allocation behavior exactly as it
/// was, at the cost of that platform's `VkDeviceMemory` allocations being
/// unable to back a sparse resource once a future session wires phases
/// 3-4 up (an acceptable, explicit limitation -- see "Memory and Buffers"
/// -- rather than this driver's entire memory allocator failing
/// outright on that platform today). Returns null on failure (`*OutFd`
/// is left at -1).
void *allocateShareableDeviceMemory(size_t Size, size_t Alignment,
                                    intptr_t *OutFd);

/// Frees storage returned by `allocateShareableDeviceMemory`: \p Data and
/// \p Size must be exactly the pointer and size that call returned, and
/// \p Fd must be exactly the handle written to its `*OutFd` (including -1,
/// the fallback-path case, in which case this is equivalent to
/// `freeDeviceMemory(Data)`).
void freeShareableDeviceMemory(void *Data, size_t Size, intptr_t Fd);

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
