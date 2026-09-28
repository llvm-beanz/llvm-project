//===- Sync.h - VkFence/VkSemaphore and queue submission -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The `VkFence` and `VkSemaphore` (V3: binary and timeline) objects,
// `vkQueueSubmit`, and (roadmap L228(h)/(i)) the per-`VkQueue`
// `QueueExecutor` that runs it (see "Queues, Scheduling, and
// Synchronization" in feme/docs/FeMeVulkanDesign.md).
//
// (Roadmap L228(h)/(i)) `vkQueueSubmit`/`vkQueueSubmit2` enqueue each
// submission onto their `VkQueue`'s own dedicated `QueueExecutor` worker
// thread and return immediately, the "dedicated executor thread per
// queue" option "Queues, Scheduling, and Synchronization" has always
// preferred over the (now retired) alternative of executing every
// submission synchronously on the calling thread. That alternative could
// never support the ordinary, spec-legal Vulkan idiom of submitting a
// whole dependent chain of work up front and releasing it with one later
// host semaphore/fence signal
// (`dEQP-VK.synchronization*.timeline_semaphore.{one_to_n,
// wait_before_signal}`, roadmap L228(h)/(i)): a synchronous
// `vkQueueSubmit` that blocks the calling thread on an unmet wait before
// returning can never let that same thread reach the later call that
// would satisfy it.
//
// Because two different queues' own `QueueExecutor`s are now genuinely
// independent `std::thread`s, every synchronization primitive they touch
// needs real cross-thread protection, not just the timeline semaphore
// (roadmap L228(a)) that needed it first: `Fence`, and a *binary*
// `Semaphore`'s side of the class, each keep their state behind a mutex
// and condition variable and offer a genuinely blocking wait, exactly the
// design this file's own comment used to say only a timeline semaphore
// needed ("Queues, Scheduling, and Synchronization" has been corrected to
// match). A single-queue submission chain observes no behavioral change
// at all: one `QueueExecutor` thread draining its own FIFO task queue in
// order reproduces the old synchronous ordering exactly, just off the
// calling thread.
//
// Every blocking wait in this file (`Fence::wait`, `Semaphore`'s
// `waitTimeline`/`waitAndConsumeBinary`, `QueueExecutor::waitIdle`) is
// clamped to `SafetyNetTimeoutNs` below regardless of what the caller
// asked for (including a literal `UINT64_MAX` "wait forever" sentinel):
// generous enough for any real cross-thread dependency this ICD has no
// real device latency to actually need, but bounded so an unrelated FeMe
// bug that genuinely never signals the awaited value fails that one call
// instead of hanging the calling process (and, transitively, any batch
// test harness driving many cases through one process) forever -- this
// software driver's analogue of a real GPU driver's hardware TDR
// (timeout-detection-and-recovery).
//
// Device loss is latched once (`Device::isLost`/`markLost`, `Objects.h`):
// any `QueueExecutor` task that fails (an unmet wait past its own safety
// net, or a command-buffer execution failure) marks its device lost
// rather than trying to report the failure back through the
// already-returned `vkQueueSubmit` call that enqueued it; every
// synchronization entry point that can legally return
// `VK_ERROR_DEVICE_LOST` checks that flag first.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SYNC_H
#define FEME_LIB_VULKAN_SYNC_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace feme::vulkan {

/// (Roadmap L228(h)/(i)) The longest any blocking wait in this file ever
/// keeps a thread parked on an unmet condition, regardless of what the
/// caller asked for -- see the file comment.
constexpr uint64_t SafetyNetTimeoutNs = 5'000'000'000ULL;

/// (Roadmap L228(h)/(i)) The bound `QueueExecutor::waitIdle` (and thus
/// `vkQueueWaitIdle`/`vkDeviceWaitIdle`) uses instead of
/// `SafetyNetTimeoutNs` directly -- see that method's own comment for why
/// it must be a generous multiple of a single blocking wait's own bound,
/// not the same value.
constexpr uint64_t QueueIdleSafetyNetTimeoutNs = 4 * SafetyNetTimeoutNs;

/// A `VkFence`: host synchronization state. Signaled by a `QueueExecutor`
/// task running on a `VkQueue`'s own worker thread, observed by
/// `vkGetFenceStatus`/`vkWaitForFences`/`vkQueueWaitIdle`/
/// `vkDeviceWaitIdle` on the calling host thread -- genuinely
/// cross-thread since roadmap L228(h)/(i), so kept behind a mutex and
/// condition variable like `Semaphore` below.
class Fence {
public:
  explicit Fence(bool Signaled) : Signaled(Signaled) {}

  bool isSignaled() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Signaled;
  }
  void signal() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Signaled = true;
    }
    CV.notify_all();
  }
  void reset() {
    std::lock_guard<std::mutex> Lock(Mutex);
    Signaled = false;
  }

  /// Blocks the calling thread until this fence is signaled or \p
  /// TimeoutNs nanoseconds elapse (already clamped to `SafetyNetTimeoutNs`
  /// by the caller, matching `vkWaitForFences`'s own `applyWaitSafetyNet`
  /// use for semaphores). Returns whether it was actually signaled (false
  /// only on a genuine timeout).
  bool wait(uint64_t TimeoutNs) const {
    std::unique_lock<std::mutex> Lock(Mutex);
    return CV.wait_for(Lock, std::chrono::nanoseconds(TimeoutNs),
                        [this] { return Signaled; });
  }

private:
  bool Signaled;
  mutable std::mutex Mutex;
  mutable std::condition_variable CV;
};

/// A `VkEvent`: device-set/reset state participating in command execution
/// (V3, per "Queues, Scheduling, and Synchronization": "An event stores
/// device-set/reset state and participates in command execution"),
/// settable from either the host (`vkSetEvent`/`vkResetEvent`) or a command
/// buffer (`vkCmdSetEvent`/`vkCmdResetEvent`). Unlike a fence or semaphore,
/// nothing consumes an event's state on a successful wait -- it stays
/// signaled until something explicitly resets it.
class Event {
public:
  explicit Event(bool Signaled = false) : Signaled(Signaled) {}

  bool isSignaled() const { return Signaled; }
  void set() { Signaled = true; }
  void reset() { Signaled = false; }

private:
  bool Signaled;
};

/// A `VkSemaphore`: either binary (unsignaled/signaled, consumed by a
/// wait) or timeline (a monotonically increasing 64-bit counter), per
/// "Queues, Scheduling, and Synchronization". Which kind this is is fixed
/// at creation (`VkSemaphoreTypeCreateInfo::semaphoreType`) and never
/// changes.
class Semaphore {
public:
  Semaphore(bool Timeline, uint64_t InitialValue)
      : Timeline(Timeline), Value(InitialValue) {}

  bool isTimeline() const { return Timeline; }

  /// Binary semaphores only: whether this is currently signaled, without
  /// blocking or consuming the signal (diagnostics/tests only -- a real
  /// wait should call `waitAndConsumeBinary` below, which blocks).
  bool isBinarySignaled() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Value != 0;
  }
  /// Binary semaphores only: signals it (`vkQueueSubmit`'s signal
  /// operation, or `vkAcquireNextImageKHR`'s, `Swapchain.cpp`) and wakes
  /// every thread currently blocked in `waitAndConsumeBinary` below.
  void signalBinary() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Value = 1;
    }
    CV.notify_all();
  }
  /// Binary semaphores only: blocks the calling thread until this
  /// semaphore is signaled or \p TimeoutNs nanoseconds elapse
  /// (`vkQueueSubmit`'s wait operation, or `vkQueuePresentKHR`'s,
  /// `Swapchain.cpp`), then atomically consumes the signal (returns it to
  /// unsignaled). Genuinely blocking since roadmap L228(h)/(i): the
  /// signal may now come from a *different* `VkQueue`'s own dedicated
  /// worker thread running concurrently with the caller, not just
  /// "already true by the time this now-retired synchronous,
  /// single-threaded driver reaches this check" the way it used to be.
  /// Returns whether the signal was actually observed (false only on a
  /// genuine timeout).
  bool waitAndConsumeBinary(uint64_t TimeoutNs = SafetyNetTimeoutNs) {
    std::unique_lock<std::mutex> Lock(Mutex);
    bool Reached = CV.wait_for(Lock, std::chrono::nanoseconds(TimeoutNs),
                                [this] { return Value != 0; });
    if (Reached)
      Value = 0;
    return Reached;
  }

  /// Timeline semaphores only: the current counter value
  /// (`vkGetSemaphoreCounterValue`). Locked (see the file comment: a real
  /// host thread can be concurrently updating `Value` via
  /// `signalTimeline`/`waitTimeline` below).
  uint64_t timelineValue() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    return Value;
  }
  /// Timeline semaphores only: sets the counter to \p NewValue
  /// (`vkSignalSemaphore`/`vkQueueSubmit`'s signal operation) and wakes
  /// every thread currently blocked in `waitTimeline` below. The caller is
  /// responsible for the specification's monotonically-increasing
  /// requirement; this class enforces no ordering of its own.
  void signalTimeline(uint64_t NewValue) {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Value = NewValue;
    }
    CV.notify_all();
  }
  /// Timeline semaphores only: blocks the calling thread until this
  /// semaphore's counter reaches or exceeds \p TargetValue (signaled by
  /// this same thread's own already-completed prior work, or by a genuine
  /// concurrent `signalTimeline` call from another real host thread -- see
  /// the file comment's `HostCopyThread` example) or until \p TimeoutNs
  /// nanoseconds elapse, whichever comes first. \p TimeoutNs of
  /// `UINT64_MAX` (matching `vkWaitSemaphores`'s own "wait forever"
  /// sentinel) waits with no time limit at all. Returns whether the
  /// target was actually reached (false only on a genuine timeout).
  bool waitTimeline(uint64_t TargetValue, uint64_t TimeoutNs) const {
    std::unique_lock<std::mutex> Lock(Mutex);
    auto Reached = [&] { return Value >= TargetValue; };
    if (TimeoutNs == UINT64_MAX) {
      CV.wait(Lock, Reached);
      return true;
    }
    return CV.wait_for(Lock, std::chrono::nanoseconds(TimeoutNs), Reached);
  }

private:
  bool Timeline;
  uint64_t Value;
  mutable std::mutex Mutex;
  mutable std::condition_variable CV;
};

/// (Roadmap L228(h)/(i)) A `VkQueue`'s own dedicated executor: a single
/// background `std::thread` that runs every task `vkQueueSubmit`/
/// `vkQueueSubmit2` enqueue for this queue, strictly in the order they
/// were enqueued (a plain FIFO queue processed by exactly one thread
/// reproduces the old synchronous single-thread ordering exactly), while
/// letting the calling (host) thread that enqueued them return
/// immediately -- see the file comment for why that is required, not just
/// an optimization.
///
/// `enqueue`/`waitIdle` are safe to call concurrently from any number of
/// host threads (Vulkan itself requires "host access to `queue` must be
/// externally synchronized" for a single queue, but different queues'
/// `QueueExecutor`s must not interfere with each other, and
/// `waitIdle`/`Device::waitIdle` may legitimately run on a different
/// thread than the one doing the submitting).
class QueueExecutor {
public:
  /// Each enqueued task is itself responsible for latching device loss
  /// (`Device::markLost`, `Objects.h`) if it fails -- `QueueExecutor`
  /// itself is device-agnostic, just a FIFO task runner.
  QueueExecutor() { Worker = std::thread([this] { run(); }); }

  QueueExecutor(const QueueExecutor &) = delete;
  QueueExecutor &operator=(const QueueExecutor &) = delete;

  /// Drains every task already enqueued (matching real Vulkan usage,
  /// which must not destroy a device with work still pending -- see
  /// "Object Lifetimes"), signals the worker thread to stop once its
  /// queue is empty, and joins it.
  ~QueueExecutor() {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      ShuttingDown = true;
    }
    CV.notify_all();
    Worker.join();
  }

  /// Enqueues \p Task to run on this queue's own worker thread after
  /// every task already enqueued for this queue, and returns immediately
  /// without waiting for it to run (`vkQueueSubmit`'s new, roadmap
  /// L228(h)/(i) behavior).
  void enqueue(std::function<void()> Task) {
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      Tasks.push_back(std::move(Task));
      ++Pending;
    }
    CV.notify_all();
  }

  /// Blocks the calling thread until every task enqueued so far has
  /// completed (`vkQueueWaitIdle`/`vkDeviceWaitIdle`), or
  /// `QueueIdleSafetyNetTimeoutNs` elapses -- deliberately a generous
  /// multiple of `SafetyNetTimeoutNs` (rather than that same bound), so
  /// this can never spuriously time out merely because it happened to be
  /// called at nearly the same moment some already-running task's own
  /// single blocking wait started its own, independent `SafetyNetTimeoutNs`
  /// countdown; a queue only ever fails to drain within this longer bound
  /// if a whole *chain* of tasks each separately hit their own safety net,
  /// or a task is genuinely stuck outside any bounded wait at all (a FeMe
  /// bug either way). Returns whether the queue was actually drained
  /// (false only on a genuine timeout).
  bool waitIdle() {
    std::unique_lock<std::mutex> Lock(Mutex);
    return CV.wait_for(Lock,
                        std::chrono::nanoseconds(QueueIdleSafetyNetTimeoutNs),
                        [this] { return Pending == 0; });
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

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SYNC_H
