//===- VirtualAddressReservationTest.cpp - VA reservation tests -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (Roadmap L416) Standalone tests for `VirtualAddressReservation` and
// `createShareableMemoryFd`, independent of any `VkDeviceMemory`/`VkBuffer`
// machinery -- see that header's own file comment: nothing in the ICD
// constructs either yet, this is phase-1 scaffolding ahead of a future
// session wiring it in.
//
//===----------------------------------------------------------------------===//

#include "VirtualAddressReservation.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>

using namespace feme::vulkan;

namespace {

constexpr size_t PageSize = 4096;

TEST(VirtualAddressReservationTest, DefaultConstructedIsUnreserved) {
  VirtualAddressReservation Reservation;
  EXPECT_FALSE(Reservation.isReserved());
  EXPECT_EQ(Reservation.base(), nullptr);
  EXPECT_EQ(Reservation.size(), 0u);
}

TEST(VirtualAddressReservationTest, ReserveSucceedsAndReportsStableBase) {
  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(16 * PageSize));
  EXPECT_TRUE(Reservation.isReserved());
  EXPECT_NE(Reservation.base(), nullptr);
  EXPECT_EQ(Reservation.size(), 16 * PageSize);
}

TEST(VirtualAddressReservationTest, ReserveZeroSizeFails) {
  VirtualAddressReservation Reservation;
  EXPECT_FALSE(Reservation.reserve(0));
  EXPECT_FALSE(Reservation.isReserved());
}

TEST(VirtualAddressReservationTest, ReleaseIsIdempotentAndSafeWithoutReserve) {
  VirtualAddressReservation Reservation;
  Reservation.release();
  EXPECT_FALSE(Reservation.isReserved());

  ASSERT_TRUE(Reservation.reserve(PageSize));
  Reservation.release();
  EXPECT_FALSE(Reservation.isReserved());
  Reservation.release();
  EXPECT_FALSE(Reservation.isReserved());
}

TEST(VirtualAddressReservationTest, ReserveAgainReplacesPriorReservation) {
  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(PageSize));
  void *FirstBase = Reservation.base();
  ASSERT_TRUE(Reservation.reserve(2 * PageSize));
  EXPECT_EQ(Reservation.size(), 2 * PageSize);
  // Not asserting `base()` changed (the allocator may coincidentally
  // reuse the same address); only that the object is self-consistent
  // after a second `reserve`.
  (void)FirstBase;
}

TEST(VirtualAddressReservationTest, MoveConstructTransfersReservation) {
  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(PageSize));
  void *Base = Reservation.base();

  VirtualAddressReservation Moved(std::move(Reservation));
  EXPECT_EQ(Moved.base(), Base);
  EXPECT_EQ(Moved.size(), PageSize);
  EXPECT_FALSE(Reservation.isReserved()); // NOLINT(bugprone-use-after-move)
}

TEST(VirtualAddressReservationTest, MoveAssignReleasesPriorReservation) {
  VirtualAddressReservation A;
  VirtualAddressReservation B;
  ASSERT_TRUE(A.reserve(PageSize));
  ASSERT_TRUE(B.reserve(2 * PageSize));
  void *ABase = A.base();

  B = std::move(A);
  EXPECT_EQ(B.base(), ABase);
  EXPECT_EQ(B.size(), PageSize);
  EXPECT_FALSE(A.isReserved()); // NOLINT(bugprone-use-after-move)
}

TEST(VirtualAddressReservationTest, CreateShareableMemoryFdSucceeds) {
  intptr_t Fd = createShareableMemoryFd(PageSize);
  EXPECT_GE(Fd, 0);
  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, CloseShareableMemoryFdToleratesNegative) {
  // Should not crash.
  closeShareableMemoryFd(-1);
}

TEST(VirtualAddressReservationTest, MapRangeBacksWindowWithRealPages) {
  intptr_t Fd = createShareableMemoryFd(PageSize);
  ASSERT_GE(Fd, 0);

  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(4 * PageSize));

  ASSERT_TRUE(Reservation.mapRange(PageSize, PageSize, Fd, 0));

  auto *Mapped = static_cast<uint8_t *>(Reservation.base()) + PageSize;
  std::memset(Mapped, 0x42, PageSize);
  EXPECT_EQ(Mapped[0], 0x42);
  EXPECT_EQ(Mapped[PageSize - 1], 0x42);

  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, MapRangeAliasesSameUnderlyingPages) {
  // The whole point of a shareable (not merely anonymous) backing object:
  // two independent mappings of it observe each other's writes, unlike
  // two separate `MAP_PRIVATE` mappings of the same file would after a
  // write (copy-on-write would diverge them).
  intptr_t Fd = createShareableMemoryFd(PageSize);
  ASSERT_GE(Fd, 0);

  VirtualAddressReservation A, B;
  ASSERT_TRUE(A.reserve(PageSize));
  ASSERT_TRUE(B.reserve(PageSize));
  ASSERT_TRUE(A.mapRange(0, PageSize, Fd, 0));
  ASSERT_TRUE(B.mapRange(0, PageSize, Fd, 0));

  static_cast<uint8_t *>(A.base())[0] = 0x7A;
  EXPECT_EQ(static_cast<uint8_t *>(B.base())[0], 0x7A);

  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, MapRangeAtOffsetUsesCorrectFdWindow) {
  intptr_t Fd = createShareableMemoryFd(2 * PageSize);
  ASSERT_GE(Fd, 0);

  // Seed the fd's second page distinctly via a throwaway mapping of the
  // whole object, so `mapRange`'s own `MemoryFdOffset` can be verified to
  // select the right window rather than always aliasing offset 0.
  {
    VirtualAddressReservation Seed;
    ASSERT_TRUE(Seed.reserve(2 * PageSize));
    ASSERT_TRUE(Seed.mapRange(0, 2 * PageSize, Fd, 0));
    std::memset(Seed.base(), 0x11, PageSize);
    std::memset(static_cast<uint8_t *>(Seed.base()) + PageSize, 0x22,
                PageSize);
  }

  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(PageSize));
  ASSERT_TRUE(Reservation.mapRange(0, PageSize, Fd, /*MemoryFdOffset=*/
                                    PageSize));
  EXPECT_EQ(static_cast<uint8_t *>(Reservation.base())[0], 0x22);

  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, UnmapRangeRestoresNoAccessGap) {
  intptr_t Fd = createShareableMemoryFd(PageSize);
  ASSERT_GE(Fd, 0);

  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(PageSize));
  ASSERT_TRUE(Reservation.mapRange(0, PageSize, Fd, 0));
  ASSERT_TRUE(Reservation.unmapRange(0, PageSize));

  // Not asserting a death test here (platform-dependent / slow under
  // some sanitizer configurations); `unmapRange`'s own return value
  // succeeding is the behavior under test. A death test would need
  // `EXPECT_DEATH` guarded to non-ASan/non-TSan builds, which is more
  // machinery than this new, currently-unused helper's first test pass
  // needs.
  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, MapRangeFailsWhenUnreserved) {
  intptr_t Fd = createShareableMemoryFd(PageSize);
  ASSERT_GE(Fd, 0);

  VirtualAddressReservation Reservation;
  EXPECT_FALSE(Reservation.mapRange(0, PageSize, Fd, 0));

  closeShareableMemoryFd(Fd);
}

TEST(VirtualAddressReservationTest, MapRangeFailsWhenOutOfBounds) {
  intptr_t Fd = createShareableMemoryFd(PageSize);
  ASSERT_GE(Fd, 0);

  VirtualAddressReservation Reservation;
  ASSERT_TRUE(Reservation.reserve(PageSize));
  EXPECT_FALSE(Reservation.mapRange(PageSize, PageSize, Fd, 0));
  EXPECT_FALSE(Reservation.mapRange(1, PageSize, Fd, 0));

  closeShareableMemoryFd(Fd);
}

} // namespace
