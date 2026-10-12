//===- VirtualAddressReservation.cpp - Sparse VA reservations -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VirtualAddressReservation.h"

#if !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace feme::vulkan {

#if defined(_WIN32)

// Not yet implemented on Windows: see this header's own file comment.
// Reporting clean failure here (rather than, say, `#error`-ing out the
// whole translation unit) keeps this new, currently-unused file buildable
// everywhere this driver targets, matching how other not-yet-implemented
// functionality in this ICD validates and reports failure instead of
// failing to compile.
intptr_t createShareableMemoryFd(size_t) { return -1; }
void closeShareableMemoryFd(intptr_t) {}
bool VirtualAddressReservation::reserve(size_t) { return false; }
bool VirtualAddressReservation::mapRange(size_t, size_t, intptr_t, size_t) {
  return false;
}
bool VirtualAddressReservation::unmapRange(size_t, size_t) { return false; }
void VirtualAddressReservation::release() {}

#else // POSIX

intptr_t createShareableMemoryFd(size_t Size) {
  int Fd = memfd_create("feme-vulkan-sparse", 0);
  if (Fd < 0)
    return -1;
  if (ftruncate(Fd, static_cast<off_t>(Size)) != 0) {
    close(Fd);
    return -1;
  }
  return Fd;
}

void closeShareableMemoryFd(intptr_t Fd) {
  if (Fd >= 0)
    close(static_cast<int>(Fd));
}

bool VirtualAddressReservation::reserve(size_t NewSize) {
  release();
  if (NewSize == 0)
    return false;
  // `PROT_NONE` + `MAP_NORESERVE`: a pure virtual-address-space claim, no
  // physical backing and no commit-charge accounting against it, since
  // every byte of it is expected to stay unmapped until a `mapRange` call
  // backs a sub-range with real pages. `MAP_PRIVATE` (rather than
  // `MAP_SHARED`) is arbitrary here -- an unbacked `PROT_NONE` mapping is
  // never actually read or written, so this choice has no observable
  // effect; it is the ordinary default for an anonymous mapping nothing
  // else maps a second time.
  void *Addr = mmap(nullptr, NewSize, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (Addr == MAP_FAILED)
    return false;
  Base = Addr;
  Size = NewSize;
  return true;
}

bool VirtualAddressReservation::mapRange(size_t Offset, size_t Length,
                                          intptr_t MemoryFd,
                                          size_t MemoryFdOffset) {
  if (!isReserved() || Length == 0 || Offset > Size ||
      Length > Size - Offset || MemoryFd < 0)
    return false;
  // `MAP_FIXED` atomically replaces whatever was mapped at this exact
  // sub-range (the initial `PROT_NONE` reservation, or a previously
  // mapped/unmapped range) with a new mapping of `MemoryFd`'s pages --
  // this is what lets a buffer's reserved address window move pages in
  // and out as `vkQueueBindSparse` rebinds ranges without ever relocating
  // the reservation's own base address.
  void *Addr =
      mmap(static_cast<uint8_t *>(Base) + Offset, Length,
           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
           static_cast<int>(MemoryFd), static_cast<off_t>(MemoryFdOffset));
  return Addr != MAP_FAILED;
}

bool VirtualAddressReservation::unmapRange(size_t Offset, size_t Length) {
  if (!isReserved() || Length == 0 || Offset > Size || Length > Size - Offset)
    return false;
  // Same `MAP_FIXED` replace-in-place technique as `mapRange`, but back
  // onto an unbacked, no-access gap rather than real pages -- restoring
  // this sub-range to the same state `reserve` originally left it in.
  void *Addr = mmap(static_cast<uint8_t *>(Base) + Offset, Length, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
                     -1, 0);
  return Addr != MAP_FAILED;
}

void VirtualAddressReservation::release() {
  if (Base)
    munmap(Base, Size);
  Base = nullptr;
  Size = 0;
}

#endif

} // namespace feme::vulkan
