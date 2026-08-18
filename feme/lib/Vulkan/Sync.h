//===- Sync.h - VkFence and queue submission ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V1 `VkFence` object and `vkQueueSubmit` (see "Queues, Scheduling, and
// Synchronization" in feme/docs/FeMeVulkanDesign.md).
//
// Deviation: `vkQueueSubmit` executes every submitted command buffer
// synchronously on the calling thread, one of the two first-implementation
// options that section explicitly allows ("execute submissions
// synchronously in `vkQueueSubmit`"). A fence is therefore always in its
// final state by the time any of `vkGetFenceStatus`/`vkWaitForFences`/
// `vkQueueWaitIdle`/`vkDeviceWaitIdle` could observe it, so none of those
// ever actually block -- there is nothing left to wait for. Binary/timeline
// semaphores are not implemented at all (no `VkSemaphore` exists in the
// object model yet), so a submission's wait/signal semaphore counts must
// be zero.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SYNC_H
#define FEME_LIB_VULKAN_SYNC_H

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

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SYNC_H
