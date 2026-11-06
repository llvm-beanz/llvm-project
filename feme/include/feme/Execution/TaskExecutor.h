//===- TaskExecutor.h - API-neutral single-worker task queue -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS2(a) ("Extract task execution and retained work storage
// from Sync.*, ...", feme/docs/WARPRoadmap.md): `feme::vulkan::
// QueueExecutor` (`feme/lib/Vulkan/Sync.h`) -- a `VkQueue`'s own dedicated
// background worker thread that runs every task enqueued to it strictly
// in FIFO order while letting the enqueuing thread return immediately --
// never actually depended on any Vulkan type. It is a plain,
// general-purpose single-worker task queue: `std::function<void()>` in,
// nothing Vulkan-specific out. This header promotes that task-runner
// itself, renamed `TaskExecutor` to avoid the Vulkan-specific "queue"
// name, into the proposed small `FeMeExecution` library (see "Shared
// extraction opportunities" in FeMeWARPDesign.md) -- the same "task
// dispatch/join" primitive a future Direct3D command-queue frontend would
// also need, without requiring it to adopt Vulkan's own `VkQueue`/
// `VkFence`/semaphore object model.
//
// Unlike the original `QueueExecutor::waitIdle()` (no arguments, which
// always consulted `getQueueIdleSafetyNetTimeoutNs()`, a Vulkan-specific
// policy function), `TaskExecutor::waitIdle` takes its timeout explicitly
// from the caller -- this class knows nothing about Vulkan's own
// safety-net-timeout policy. `feme::vulkan::QueueExecutor` becomes a thin
// wrapper that supplies that policy's value and otherwise forwards
// unchanged (`Sync.h`); every existing Vulkan caller's own
// `enqueue`/`waitIdle()` call sites are unaffected by this extraction.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_EXECUTION_TASKEXECUTOR_H
#define FEME_EXECUTION_TASKEXECUTOR_H

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace feme::execution {

/// A single dedicated background `std::thread` that runs every task
/// appended to it (`enqueue`) strictly in the order they were enqueued (a
/// plain FIFO queue processed by exactly one thread reproduces
/// synchronous single-thread ordering exactly), while letting the
/// enqueuing thread return immediately. See `feme::vulkan::QueueExecutor`
/// (`Sync.h`)'s own file comment for the full cross-thread design
/// rationale this was originally built to satisfy.
///
/// `enqueue`/`waitIdle` are safe to call concurrently from any number of
/// threads; a single `TaskExecutor` instance's own tasks always run in
/// the order they were enqueued, regardless of how many threads are
/// enqueuing them concurrently.
class TaskExecutor {
public:
  /// Each enqueued task is itself responsible for reporting/handling its
  /// own failure -- `TaskExecutor` itself is purpose-agnostic, just a
  /// FIFO task runner.
  TaskExecutor() {
    Worker = std::thread([this] { run(); });
  }

  TaskExecutor(const TaskExecutor &) = delete;
  TaskExecutor &operator=(const TaskExecutor &) = delete;

  /// Drains every task already enqueued, signals the worker thread to
  /// stop once its queue is empty, and joins it. Does NOT protect any
  /// resource a not-yet-run task might reference -- callers needing that
  /// guarantee need their own keep-alive scheme (`feme::vulkan::
  /// PendingExecutionRefCounted`, `ExecutionLifetime.h`, is one such
  /// scheme, layered on top of this class, not inside it).
  ~TaskExecutor() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      ShuttingDown = true;
    }
    CV.notify_all();
    Worker.join();
  }

  /// Enqueues \p Task to run on this executor's own worker thread after
  /// every task already enqueued, and returns immediately without waiting
  /// for it to run.
  void enqueue(std::function<void()> Task) {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Tasks.push_back(std::move(Task));
      ++Pending;
    }
    CV.notify_all();
  }

  /// Blocks the calling thread until every task enqueued so far has
  /// completed, or \p Timeout elapses. Returns whether the queue was
  /// actually drained (false only on a genuine timeout).
  bool waitIdle(std::chrono::nanoseconds Timeout) {
    std::unique_lock<std::mutex> Lock(Mutex);
    return CV.wait_for(Lock, Timeout, [this] { return Pending == 0; });
  }

private:
  void run() {
    while (true) {
      std::function<void()> Task;
      {
        std::unique_lock<std::mutex> Lock(Mutex);
        CV.wait(Lock, [this] { return !Tasks.empty() || ShuttingDown; });
        if (Tasks.empty())
          return; // ShuttingDown, and every task already drained.
        Task = std::move(Tasks.front());
        Tasks.pop_front();
      }
      Task();
      {
        std::lock_guard<std::mutex> Lock(Mutex);
        --Pending;
      }
      CV.notify_all();
    }
  }

  std::mutex Mutex;
  std::condition_variable CV;
  std::deque<std::function<void()>> Tasks;
  size_t Pending = 0; // Tasks enqueued but not yet finished running.
  bool ShuttingDown = false;
  // Must be declared (and therefore constructed) last: `run` touches
  // every member above as soon as the thread starts, so they must all
  // already be alive first.
  std::thread Worker;
};

} // namespace feme::execution

#endif // FEME_EXECUTION_TASKEXECUTOR_H
