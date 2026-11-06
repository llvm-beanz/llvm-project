//===- DeferredDestroyRefCountedTest.cpp - Tests for
// feme::execution::DeferredDestroyRefCounted ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS2(b): exercises the extracted, portable
// `DeferredDestroyRefCounted` template directly, instantiated on a
// non-Vulkan payload type (a plain `int`) to prove it is genuinely
// parameterized rather than merely renamed from its original
// `VkAllocationCallbacks`-specific shape. `feme::vulkan::
// PendingExecutionRefCounted` is now a type alias instantiating this same
// template on `const VkAllocationCallbacks *`; its own existing coverage
// (`feme/unittests/Vulkan/SyncTest.cpp`'s `ExecutionLifetimeTest` suite,
// unchanged by this extraction) continues to exercise that instantiation
// at the Vulkan level.
//
//===----------------------------------------------------------------------===//

#include "feme/Execution/DeferredDestroyRefCounted.h"

#include "gtest/gtest.h"

using namespace feme::execution;

namespace {

using IntPayloadRefCounted = DeferredDestroyRefCounted<int>;

TEST(DeferredDestroyRefCountedTest, DestroyWithNoPendingTasksFreesImmediately) {
  IntPayloadRefCounted Obj;
  EXPECT_TRUE(Obj.requestDestroy(0));
}

TEST(DeferredDestroyRefCountedTest, DestroyDefersUntilRetainedTaskReleases) {
  IntPayloadRefCounted Obj;

  Obj.retainForExecution();
  EXPECT_FALSE(Obj.requestDestroy(42));

  int Payload = 0;
  EXPECT_TRUE(Obj.releaseAndTakeDeferredDestroy(Payload));
  EXPECT_EQ(Payload, 42);
}

TEST(DeferredDestroyRefCountedTest, DestroyDefersUntilAllRetainedTasksRelease) {
  IntPayloadRefCounted Obj;

  Obj.retainForExecution();
  Obj.retainForExecution();
  EXPECT_FALSE(Obj.requestDestroy(7));

  // Releasing the first of two retains must not yet trigger the deferred
  // destroy -- the second task might still be reading this object.
  int Payload = 0;
  EXPECT_FALSE(Obj.releaseAndTakeDeferredDestroy(Payload));
  EXPECT_EQ(Payload, 0);

  EXPECT_TRUE(Obj.releaseAndTakeDeferredDestroy(Payload));
  EXPECT_EQ(Payload, 7);
}

TEST(DeferredDestroyRefCountedTest, ReleaseWithoutDestroyRequestedDoesNotFree) {
  IntPayloadRefCounted Obj;

  Obj.retainForExecution();
  int Payload = 0;
  // The task finishes before any destroy call occurs at all -- nothing
  // should be freed, and no payload should be reported.
  EXPECT_FALSE(Obj.releaseAndTakeDeferredDestroy(Payload));
  EXPECT_EQ(Payload, 0);

  // Now the object is genuinely idle again: a later destroy call must see
  // zero pending tasks and free immediately.
  EXPECT_TRUE(Obj.requestDestroy(99));
}

TEST(DeferredDestroyRefCountedTest, SupportsAPointerPayloadTypeToo) {
  // Confirms the template is not accidentally limited to value types --
  // the one real-world consumer (`feme::vulkan::PendingExecutionRefCounted`)
  // instantiates it on a pointer type.
  using PtrPayloadRefCounted = DeferredDestroyRefCounted<int *>;
  int Sentinel = 0;

  PtrPayloadRefCounted Obj;
  Obj.retainForExecution();
  EXPECT_FALSE(Obj.requestDestroy(&Sentinel));

  int *Payload = nullptr;
  EXPECT_TRUE(Obj.releaseAndTakeDeferredDestroy(Payload));
  EXPECT_EQ(Payload, &Sentinel);
}

} // namespace
