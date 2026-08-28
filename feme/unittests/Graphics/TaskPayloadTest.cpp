//===- TaskPayloadTest.cpp - Tests for TaskPayloadBuilder -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/TaskPayload.h"

#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(TaskPayloadTest, StartsZeroInitialized) {
  TaskPayloadBuilder Builder(8);
  EXPECT_EQ(Builder.getMaxPayloadBytes(), 8u);
  for (uint8_t Byte : Builder.getBytes())
    EXPECT_EQ(Byte, 0u);
}

TEST(TaskPayloadTest, WriteAndReadRoundTrip) {
  TaskPayloadBuilder Builder(8);
  EXPECT_TRUE(Builder.write(2, {0x11, 0x22, 0x33}));
  EXPECT_EQ(Builder.read(2, 3), (llvm::ArrayRef<uint8_t>{0x11, 0x22, 0x33}));
  // Untouched bytes on either side stay zero.
  EXPECT_EQ(Builder.read(0, 2), (llvm::ArrayRef<uint8_t>{0x00, 0x00}));
  EXPECT_EQ(Builder.read(5, 3), (llvm::ArrayRef<uint8_t>{0x00, 0x00, 0x00}));
}

TEST(TaskPayloadTest, WriteRejectsAnOutOfBoundsRange) {
  TaskPayloadBuilder Builder(4);
  EXPECT_FALSE(Builder.write(3, {1, 2})); // would run to byte 5, out of 4.
  EXPECT_FALSE(Builder.write(5, {1}));    // offset itself is out of range.
  // Rejected writes leave storage untouched.
  for (uint8_t Byte : Builder.getBytes())
    EXPECT_EQ(Byte, 0u);
}

TEST(TaskPayloadTest, ReadRejectsAnOutOfBoundsRange) {
  TaskPayloadBuilder Builder(4);
  EXPECT_TRUE(Builder.read(3, 2).empty());
  EXPECT_TRUE(Builder.read(5, 1).empty());
}

TEST(TaskPayloadTest, WriteAtExactBoundaryFits) {
  TaskPayloadBuilder Builder(4);
  EXPECT_TRUE(Builder.write(0, {1, 2, 3, 4}));
  EXPECT_EQ(Builder.getBytes(), (llvm::ArrayRef<uint8_t>{1, 2, 3, 4}));
}

} // namespace
