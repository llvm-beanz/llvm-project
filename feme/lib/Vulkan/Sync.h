//===- Sync.h - VkFence/VkSemaphore and queue submission -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The `VkFence` and `VkSemaphore` (V3: binary and timeline) objects and
// `vkQueueSubmit` (see "Queues, Scheduling, and Synchronization" in
// feme/docs/FeMeVulkanDesign.md).
//
// Deviation: `vkQueueSubmit` executes every submitted command buffer
// synchronously on the calling thread, one of the two first-implementation
// options that section explicitly allows ("execute submissions
// synchronously in `vkQueueSubmit`"). A fence is therefore always in its
// final state by the time any of `vkGetFenceStatus`/`vkWaitForFences`/
// `vkQueueWaitIdle`/`vkDeviceWaitIdle` could observe it, so none of those
// ever actually block -- there is nothing left to wait for. The same is
// true of a *binary* semaphore: core Vulkan gives it no host-facing
// signal entry point at all, so since there is only ever one queue and
// submission order is program order, a `vkQueueSubmit` that waits on one
// can only ever observe a signal from a submission that has *already
// completed* by the time this ICD sees the wait -- signaling and waiting
// are never truly concurrent for a binary semaphore here. A binary wait
// whose semaphore is not yet signaled is therefore a real application
// ordering error, reported as `VK_ERROR_INITIALIZATION_FAILED`.
//
// (Roadmap L228) A *timeline* semaphore is different: `vkSignalSemaphore`
// is a genuine host-side entry point a real, independent application
// thread can call concurrently with another thread's blocked-on-it
// `vkQueueSubmit`/`vkWaitSemaphores` call (`dEQP-VK.synchronization.
// timeline_semaphore.device_host.*`'s own `HostCopyThread` does exactly
// this: it waits on the semaphore reaching a device-signaled value, then
// signals a further value the *next* `vkQueueSubmit` call blocks on, from
// a real, separate `std::thread`/`de::Thread`). Treating this exactly
// like a binary semaphore's instantaneous, non-blocking check -- as this
// file's first implementation did -- is wrong on two counts: it reports
// the still-common case as a spurious ordering-error failure instead of
// genuinely waiting for the other thread to catch up, and unguarded
// concurrent reads/writes of the same counter from two real host threads
// (the submitting thread's read, the signaling thread's write) is a data
// race regardless of which value either observes. `Semaphore` therefore
// keeps its counter behind a mutex and condition variable for a timeline
// semaphore specifically (matching "Queues, Scheduling, and
// Synchronization"'s own original "monotonically changing state under a
// mutex and condition variable" design), and `vkQueueSubmit`/
// `vkQueueSubmit2`/`vkWaitSemaphores` genuinely block on it rather than
// checking once and failing.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SYNC_H
#define FEME_LIB_VULKAN_SYNC_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace feme::vulkan {

/// A `VkFence`: host synchronization state, always already resolved by the
/// time any command observes it (see the file comment's synchronous
/// `vkQueueSubmit` deviation).
class Fence {
public:
  explicit Fence(bool Signaled) : Signaled(Signaled) {}

  bool isSignaled() const { return Signaled; }
  void signal() { Signaled = true; }
  void reset() { Signaled = false; }

private:
  bool Signaled;
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

  /// Binary semaphores only: whether this is currently signaled. Never
  /// touched by more than one thread at a time (see the file comment), so
  /// unlike the timeline members below, no lock is needed.
  bool isBinarySignaled() const { return Value != 0; }
  /// Binary semaphores only: signals it (`vkQueueSubmit`'s signal
  /// operation).
  void signalBinary() { Value = 1; }
  /// Binary semaphores only: consumes its signal (`vkQueueSubmit`'s wait
  /// operation), returning whether it was signaled to begin with.
  bool waitAndConsumeBinary() {
    if (Value == 0)
      return false;
    Value = 0;
    return true;
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

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SYNC_H
