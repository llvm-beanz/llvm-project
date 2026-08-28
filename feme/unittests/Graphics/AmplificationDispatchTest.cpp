//===- AmplificationDispatchTest.cpp - Tests for AmplificationDispatchQueue =//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/AmplificationDispatch.h"

#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace feme::graphics;

namespace {

TEST(AmplificationDispatchTest, AcceptsAGroupCountWithinBothBounds) {
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {64, 64, 64};
  Limits.MaxTotalGroupCount = 1000;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({4, 5, 6}, Limits);
  ASSERT_THAT_EXPECTED(Queue, llvm::Succeeded());
  EXPECT_EQ(Queue->getGroupCount(), (std::array<uint32_t, 3>{4, 5, 6}));
  EXPECT_EQ(Queue->size(), 120u);
}

TEST(AmplificationDispatchTest, RejectsAPerDimensionCountBeyondTheLimit) {
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {8, 8, 8};
  Limits.MaxTotalGroupCount = 10000;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({9, 1, 1}, Limits);
  ASSERT_FALSE((bool)Queue);
  std::string Message = llvm::toString(Queue.takeError());
  EXPECT_NE(Message.find("maxMeshWorkGroupCount"), std::string::npos);
}

TEST(AmplificationDispatchTest,
     RejectsATotalCountBeyondTheLimitEvenIfEachDimensionFits) {
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {100, 100, 100};
  Limits.MaxTotalGroupCount = 50;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({10, 10, 1}, Limits);
  ASSERT_FALSE((bool)Queue);
  std::string Message = llvm::toString(Queue.takeError());
  EXPECT_NE(Message.find("maxMeshWorkGroupTotalCount"), std::string::npos);
}

TEST(AmplificationDispatchTest, ComputesTheProductWithoutA32BitOverflow) {
  // Each dimension alone fits comfortably in 32 bits, but their product
  // does not fit in 32 bits -- a 32-bit-truncated product could wrap back
  // under `MaxTotalGroupCount` and wrongly accept this request.
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {70000, 70000, 1};
  Limits.MaxTotalGroupCount = 100;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({70000, 70000, 1}, Limits);
  ASSERT_FALSE((bool)Queue);
  llvm::consumeError(Queue.takeError());
}

TEST(AmplificationDispatchTest, EnumeratesGroupIDsXFastestZSlowest) {
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {8, 8, 8};
  Limits.MaxTotalGroupCount = 1000;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({2, 2, 2}, Limits);
  ASSERT_THAT_EXPECTED(Queue, llvm::Succeeded());
  ASSERT_EQ(Queue->size(), 8u);

  EXPECT_EQ(Queue->getGroupID(0), (std::array<uint32_t, 3>{0, 0, 0}));
  EXPECT_EQ(Queue->getGroupID(1), (std::array<uint32_t, 3>{1, 0, 0}));
  EXPECT_EQ(Queue->getGroupID(2), (std::array<uint32_t, 3>{0, 1, 0}));
  EXPECT_EQ(Queue->getGroupID(3), (std::array<uint32_t, 3>{1, 1, 0}));
  EXPECT_EQ(Queue->getGroupID(4), (std::array<uint32_t, 3>{0, 0, 1}));
  EXPECT_EQ(Queue->getGroupID(7), (std::array<uint32_t, 3>{1, 1, 1}));
}

TEST(AmplificationDispatchTest, ZeroGroupCountIsAValidEmptyDispatch) {
  AmplificationDispatchLimits Limits;
  Limits.MaxGroupCount = {8, 8, 8};
  Limits.MaxTotalGroupCount = 1000;

  llvm::Expected<AmplificationDispatchQueue> Queue =
      AmplificationDispatchQueue::create({0, 1, 1}, Limits);
  ASSERT_THAT_EXPECTED(Queue, llvm::Succeeded());
  EXPECT_EQ(Queue->size(), 0u);
}

} // namespace
