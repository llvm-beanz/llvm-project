//===- QueryPoolTest.cpp - Unit tests for the QueryPool object model ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "QueryPool.h"

#include "gtest/gtest.h"

#include <chrono>
#include <thread>

using namespace feme::vulkan;

namespace {

TEST(QueryPoolTest, WaitAvailableReturnsImmediatelyWhenAlreadyAvailable) {
  QueryPool Pool(4, VK_QUERY_TYPE_OCCLUSION);
  Pool.begin(0);
  Pool.markAvailable(0);
  EXPECT_TRUE(Pool.waitAvailable(0, 1, /*TimeoutNs=*/1'000'000));
}

TEST(QueryPoolTest, WaitAvailableTimesOutWhenNeverMadeAvailable) {
  QueryPool Pool(4, VK_QUERY_TYPE_OCCLUSION);
  // Query 0 is reset (never begun/ended), so it never becomes available
  // on its own -- `waitAvailable` must still return (a genuine timeout),
  // not block forever.
  EXPECT_FALSE(Pool.waitAvailable(0, 1, /*TimeoutNs=*/10'000'000));
}

// (Roadmap L354) The cross-thread regression this fix addresses: a
// `QueueExecutor` worker thread completing `vkCmdEndQuery` sometime after
// `vkGetQueryPoolResults(..., VK_QUERY_RESULT_WAIT_BIT)` is already
// blocked waiting for it must still be observed, rather than that call
// racily sampling `isAvailable` once before the worker thread gets there
// (`dEQP-VK.query_pool.occlusion_query.get_results_*_wait_query_*`'s own
// failure mode before this fix).
TEST(QueryPoolTest, WaitAvailableBlocksUntilAnotherThreadMarksItAvailable) {
  QueryPool Pool(4, VK_QUERY_TYPE_OCCLUSION);
  Pool.begin(0);

  constexpr auto SignalDelay = std::chrono::milliseconds(200);
  std::thread Signaler([&] {
    std::this_thread::sleep_for(SignalDelay);
    Pool.markAvailable(0);
  });

  auto Start = std::chrono::steady_clock::now();
  EXPECT_TRUE(Pool.waitAvailable(0, 1, /*TimeoutNs=*/5'000'000'000));
  auto Elapsed = std::chrono::steady_clock::now() - Start;
  // A non-blocking (pre-fix) implementation would return instantly,
  // failing this check well before `SignalDelay` elapses.
  EXPECT_GE(Elapsed, SignalDelay);

  Signaler.join();
}

TEST(QueryPoolTest, WaitAvailableRequiresEveryQueryInRangeAvailable) {
  QueryPool Pool(4, VK_QUERY_TYPE_OCCLUSION);
  Pool.begin(0);
  Pool.begin(1);
  Pool.markAvailable(0);
  // Query 1 is still active (not yet ended): a range wait covering both
  // 0 and 1 must not report success just because query 0 alone is ready.
  EXPECT_FALSE(Pool.waitAvailable(0, 2, /*TimeoutNs=*/10'000'000));
  Pool.markAvailable(1);
  EXPECT_TRUE(Pool.waitAvailable(0, 2, /*TimeoutNs=*/10'000'000));
}

} // namespace
