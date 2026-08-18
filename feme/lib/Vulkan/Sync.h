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
// true of a semaphore: since there is only ever one queue and submission
// order is program order, a `vkQueueSubmit` that waits on a semaphore can
// only observe one signaled by a submission that has *already completed*
// by the time this ICD sees the wait -- signaling and waiting are never
// truly concurrent here. A wait whose semaphore is not yet signaled is
// therefore a real application ordering error, not something this driver's
// execution model can ever resolve by waiting longer; `vkQueueSubmit`
// reports it as `VK_ERROR_INITIALIZATION_FAILED` instead of one of
// Vulkan's own deadlock-only failure modes, since no deadlock detection
// timer exists to produce those. The host wait functions
// (`vkWaitSemaphores`/`vkGetSemaphoreCounterValue`/`vkSignalSemaphore`) are
// unaffected by this: they only ever observe already-resolved state, for
// the same reason.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SYNC_H
#define FEME_LIB_VULKAN_SYNC_H

#include <cstdint>

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

  /// Binary semaphores only: whether this is currently signaled.
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
  /// (`vkGetSemaphoreCounterValue`).
  uint64_t timelineValue() const { return Value; }
  /// Timeline semaphores only: sets the counter to \p NewValue
  /// (`vkSignalSemaphore`/`vkQueueSubmit`'s signal operation). The caller
  /// is responsible for the specification's monotonically-increasing
  /// requirement; this class enforces no ordering of its own.
  void signalTimeline(uint64_t NewValue) { Value = NewValue; }

private:
  bool Timeline;
  uint64_t Value;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SYNC_H
