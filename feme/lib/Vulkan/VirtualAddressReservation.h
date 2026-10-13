//===- VirtualAddressReservation.h - Sparse VA reservations -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap L416) Building block for a future genuinely-sparse
// `vkGetBufferDeviceAddress`: see "Memory and Buffers" in
// feme/docs/FeMeVulkanDesign.md for the full design and the phased plan
// this is phase 1 of. In short -- a device address is handed to the
// application and may be dereferenced at any later point, including one
// where the buffer's residency has since changed via `vkQueueBindSparse`,
// so a single flat host pointer (today's `Buffer::data()`-based answer)
// cannot represent a partially-resident buffer. The fix is to reserve one
// *virtual* address range spanning the buffer's entire declared size up
// front, with no physical backing, and remap physical pages into
// sub-ranges of it as binds/unbinds happen -- exactly what
// `VirtualAddressReservation` below provides.
//
// This header is new, currently-unused infrastructure: nothing in the ICD
// constructs a `VirtualAddressReservation` yet. It exists in isolation,
// with its own unit tests, ahead of a future session wiring it into
// `DeviceMemory`/`Buffer`/`vkQueueBindSparse` -- the same
// zero-behavior-change-first approach `MemoryBinding.h` (roadmap H46) took
// for `ResourceBindings`.
//
// POSIX only for now: implemented via `mmap` with `memfd_create` on Linux
// and unlinked `shm_open` objects on other POSIX platforms.
// Windows support (an equivalent two-view design via
// `CreateFileMapping`/`MapViewOfFile3`) is a future increment --
// `createShareableMemoryFd`/`VirtualAddressReservation::reserve` simply
// report failure there today, matching this driver's existing "validate,
// report failure, do not crash" convention for not-yet-implemented
// functionality on a given platform.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_VIRTUALADDRESSRESERVATION_H
#define FEME_LIB_VULKAN_VIRTUALADDRESSRESERVATION_H

#include <cstdint>
#include <cstddef>

namespace feme::vulkan {

/// Creates a \p Size -byte anonymous, shareable memory object: one whose
/// pages can be mapped at more than one virtual address simultaneously
/// (via repeated calls to \c VirtualAddressReservation::mapRange with the
/// same handle), unlike a plain heap allocation. Returns -1 on failure, or
/// on a platform that does not support this (see this file's own
/// comment). The caller owns the returned handle and must release it with
/// \c closeShareableMemoryFd once no longer needed.
intptr_t createShareableMemoryFd(size_t Size);

/// Releases a handle returned by \c createShareableMemoryFd. A no-op if
/// \p Fd is negative.
void closeShareableMemoryFd(intptr_t Fd);

/// A contiguous virtual address range reserved up front, with sub-ranges
/// independently mappable onto (or back to no-access) a shareable memory
/// object's pages without ever moving the reservation's own base address.
/// See this file's own comment for the design this supports.
///
/// Not copyable (the reservation is a unique OS-level resource); movable.
class VirtualAddressReservation {
public:
  VirtualAddressReservation() = default;
  ~VirtualAddressReservation() { release(); }

  VirtualAddressReservation(const VirtualAddressReservation &) = delete;
  VirtualAddressReservation &
  operator=(const VirtualAddressReservation &) = delete;

  VirtualAddressReservation(VirtualAddressReservation &&Other) noexcept
      : Base(Other.Base), Size(Other.Size) {
    Other.Base = nullptr;
    Other.Size = 0;
  }
  VirtualAddressReservation &
  operator=(VirtualAddressReservation &&Other) noexcept {
    if (this != &Other) {
      release();
      Base = Other.Base;
      Size = Other.Size;
      Other.Base = nullptr;
      Other.Size = 0;
    }
    return *this;
  }

  /// Reserves \p Size bytes of virtual address space with no read/write
  /// access and no physical backing: any access faults until a sub-range
  /// is mapped via \c mapRange. Releases any prior reservation this
  /// object held first. Returns false on failure (this object is left
  /// unreserved).
  bool reserve(size_t Size);

  /// True if \c reserve has succeeded and \c release has not since been
  /// called.
  bool isReserved() const { return Base != nullptr; }

  /// The reservation's base address, or null if unreserved. Stable for
  /// the reservation's lifetime: unlike a plain heap allocation, this
  /// never changes as sub-ranges are mapped/unmapped below.
  void *base() const { return Base; }
  size_t size() const { return Size; }

  /// Backs resource-space sub-range `[Offset, Offset + Length)` of this
  /// reservation with a `Length`-byte window of \p MemoryFd 's pages
  /// starting at \p MemoryFdOffset, replacing whatever was mapped there
  /// before (including a prior no-access gap left by \c unmapRange or the
  /// initial \c reserve). \p MemoryFd must be a handle from \c
  /// createShareableMemoryFd (or equivalent), kept alive by the caller
  /// for at least as long as this mapping exists. `[Offset, Offset +
  /// Length)` must fall entirely within `[0, size())`. Returns false on
  /// failure.
  bool mapRange(size_t Offset, size_t Length, intptr_t MemoryFd,
                size_t MemoryFdOffset);

  /// Restores `[Offset, Offset + Length)` to a no-access gap, as if never
  /// passed to \c mapRange, without disturbing any other sub-range.
  /// Returns false on failure.
  bool unmapRange(size_t Offset, size_t Length);

  /// Releases the entire reservation (and whatever is currently mapped
  /// within it). Safe to call when already unreserved (a no-op).
  void release();

private:
  void *Base = nullptr;
  size_t Size = 0;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_VIRTUALADDRESSRESERVATION_H
