//===- TaskExecutorTest.cpp - Tests for feme::execution::TaskExecutor ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS2(a) ("Extract task execution and retained work storage
// from Sync.*, ..."): directly exercises the extracted portable FIFO task
// runner without any Vulkan dependency. `feme/unittests/Vulkan/
// SyncTest.cpp` (unchanged by this extraction) continues to cover
// `feme::vulkan::QueueExecutor` as a thin forwarder through its own
// submission/fence/semaphore-level tests.
//
//===----------------------------------------------------------------------===//

#include "feme/Execution/TaskExecutor.h"

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace feme::execution;

namespace {

TEST(TaskExecutorTest, EnqueueReturnsWithoutWaitingForTaskToRun) {
  TaskExecutor Executor;
  std::mutex Mutex;
  std::condition_variable Started;
  bool TaskMayProceed = false;
  bool TaskStarted = false;

  Executor.enqueue([&] {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      TaskStarted = true;
    }
    Started.notify_all();
    std::unique_lock<std::mutex> Lock(Mutex);
    Started.wait(Lock, [&] { return TaskMayProceed; });
  });

  // Let the task actually start, then let it finish so the executor's own
  // destructor does not have to wait on it.
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    Started.wait(Lock, [&] { return TaskStarted; });
    TaskMayProceed = true;
  }
  Started.notify_all();
  EXPECT_TRUE(Executor.waitIdle(std::chrono::seconds(5)));
}

TEST(TaskExecutorTest, TasksRunInFIFOOrder) {
  TaskExecutor Executor;
  std::mutex Mutex;
  std::vector<int> Order;

  for (int I = 0; I < 20; ++I) {
    Executor.enqueue([&, I] {
      std::lock_guard<std::mutex> Lock(Mutex);
      Order.push_back(I);
    });
  }

  ASSERT_TRUE(Executor.waitIdle(std::chrono::seconds(5)));
  std::lock_guard<std::mutex> Lock(Mutex);
  ASSERT_EQ(Order.size(), 20u);
  for (int I = 0; I < 20; ++I)
    EXPECT_EQ(Order[I], I);
}

TEST(TaskExecutorTest, WaitIdleDrainsAllPendingTasks) {
  TaskExecutor Executor;
  std::atomic<int> Completed{0};

  for (int I = 0; I < 50; ++I)
    Executor.enqueue([&] { ++Completed; });

  EXPECT_TRUE(Executor.waitIdle(std::chrono::seconds(5)));
  EXPECT_EQ(Completed.load(), 50);

  // A second `waitIdle` with nothing pending returns immediately (true).
  EXPECT_TRUE(Executor.waitIdle(std::chrono::seconds(5)));
}

TEST(TaskExecutorTest, WaitIdleTimesOutOnAStillRunningTask) {
  TaskExecutor Executor;
  std::mutex Mutex;
  std::condition_variable Release;
  bool MayFinish = false;

  Executor.enqueue([&] {
    std::unique_lock<std::mutex> Lock(Mutex);
    Release.wait(Lock, [&] { return MayFinish; });
  });

  // The task above never finishes until released below, so a short
  // timeout must genuinely expire (return false), not spuriously drain.
  EXPECT_FALSE(Executor.waitIdle(std::chrono::milliseconds(50)));

  {
    std::lock_guard<std::mutex> Lock(Mutex);
    MayFinish = true;
  }
  Release.notify_all();
  EXPECT_TRUE(Executor.waitIdle(std::chrono::seconds(5)));
}

TEST(TaskExecutorTest, DestructorDrainsPendingTasksBeforeJoining) {
  std::atomic<int> Completed{0};
  {
    TaskExecutor Executor;
    for (int I = 0; I < 50; ++I)
      Executor.enqueue([&] { ++Completed; });
    // No explicit `waitIdle` -- the destructor itself must drain every
    // already-enqueued task before its worker thread stops.
  }
  EXPECT_EQ(Completed.load(), 50);
}

TEST(TaskExecutorTest, ConcurrentEnqueueFromMultipleThreadsIsSafe) {
  TaskExecutor Executor;
  std::atomic<int> Completed{0};
  std::vector<std::thread> Enqueuers;

  constexpr int ThreadCount = 8;
  constexpr int TasksPerThread = 50;
  for (int T = 0; T < ThreadCount; ++T) {
    Enqueuers.emplace_back([&] {
      for (int I = 0; I < TasksPerThread; ++I)
        Executor.enqueue([&] { ++Completed; });
    });
  }
  for (std::thread &Th : Enqueuers)
    Th.join();

  ASSERT_TRUE(Executor.waitIdle(std::chrono::seconds(10)));
  EXPECT_EQ(Completed.load(), ThreadCount * TasksPerThread);
}

} // namespace
