//===- DeferredDestroyRefCounted.h - Deferred-destroy keep-alive mixin --*-
// C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARP roadmap WS2(b) ("generalize `PendingExecutionRefCounted`'s
// retain/release-with-deferred-destroy pattern ... into `FeMeExecution`,
// parameterizing its opaque deferred-destroy payload type"):
// `feme::vulkan::PendingExecutionRefCounted` (`feme/lib/Vulkan/
// ExecutionLifetime.h`) is a plain retain-count/mutex/condition-free
// counter that lets any object type an async `TaskExecutor` task might
// still be reading defer its own destruction until every such task has
// finished -- the "execution-owned storage/retention" half of the
// roadmap's proposed `FeMeExecution` library (see "Sharing contract" in
// WARPRoadmap.md). Its only Vulkan-specific detail was hardcoding `const
// VkAllocationCallbacks *` as the opaque payload remembered between a
// deferred `requestDestroy` and the later `releaseAndTakeDeferredDestroy`
// that actually permits the free -- every one of its twelve Vulkan
// consumers (`Pipeline`, `RenderPass`, `Framebuffer`, `Buffer`, `Image`,
// `ImageView`, `DescriptorSet`, `BufferView`, `Sampler`, `Event`,
// `QueryPool`, `DeviceMemory`, `CommandBuffer`) use that exact same
// payload type, so parameterizing it on a template parameter instead
// changes no behavior for any of them.
//
// `feme::vulkan::PendingExecutionRefCounted` becomes a type alias for
// `DeferredDestroyRefCounted<const VkAllocationCallbacks *>` -- a plain
// alias, not a wrapper, since (unlike WS2(a)'s `QueueExecutor`/
// `TaskExecutor` split) there is no Vulkan-specific policy layered on
// top of this class: every method's behavior is identical for every
// payload type.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_EXECUTION_DEFERREDDESTROYREFCOUNTED_H
#define FEME_EXECUTION_DEFERREDDESTROYREFCOUNTED_H

#include <mutex>

namespace feme::execution {

/// A retain-counted object that lets a `vkDestroy*`-style entry point
/// defer its own destruction until every `TaskExecutor` task that
/// referenced it beforehand (`retainForExecution`) has finished running
/// (`releaseAndTakeDeferredDestroy`), instead of freeing memory such a
/// task might still be reading. \p PayloadT is the opaque value a
/// deferred `requestDestroy` call remembers and later hands back to the
/// caller that ends up performing the actual free -- `feme::vulkan::
/// PendingExecutionRefCounted` uses `const VkAllocationCallbacks *`, but
/// nothing about this class's own logic depends on that choice. \p
/// PayloadT must be default-constructible and copyable; a default-
/// constructed value is never itself used as a valid payload, only as
/// the "not yet set" placeholder before a `requestDestroy` call occurs.
///
/// All public methods are safe to call from any thread (the host thread
/// doing a `vkDestroy*`-equivalent call, or a `TaskExecutor` worker
/// thread finishing a task), and are `const` so they remain usable
/// through a `const T *` (recorded commands reference several consuming
/// types that way).
template <typename PayloadT> class DeferredDestroyRefCounted {
public:
  /// Called once per `TaskExecutor` task that references this object,
  /// synchronously on the submitting host thread, before that task is
  /// enqueued -- this must happen before any other thread can possibly
  /// request this object's destruction.
  void retainForExecution() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    ++PendingTasks;
  }

  /// Called by a `TaskExecutor` task once it finishes running (whether it
  /// succeeded, failed, or skipped work for any other reason). If a
  /// destroy call already asked to destroy this object while this was
  /// the last outstanding task (`requestDestroy` below returned false),
  /// returns true and sets \p OutPayload to the value that call was
  /// given -- the caller must then perform the actual free using it.
  /// Otherwise returns false and leaves \p OutPayload untouched.
  bool releaseAndTakeDeferredDestroy(PayloadT &OutPayload) const {
    std::lock_guard<std::mutex> Lock(Mutex);
    --PendingTasks;
    if (PendingTasks != 0 || !DestroyRequested)
      return false;
    OutPayload = PendingDestroyPayload;
    return true;
  }

  /// A destroy entry point: returns true if the caller may free this
  /// object immediately (the ordinary case: every `TaskExecutor` task
  /// that ever referenced it has already finished running). Returns
  /// false if at least one such task has not finished yet -- in that
  /// case this object defers its own destruction (remembering \p
  /// Payload) until that task releases it
  /// (`releaseAndTakeDeferredDestroy` above), instead of letting the
  /// caller free memory a `TaskExecutor` worker thread might still be
  /// reading.
  bool requestDestroy(PayloadT Payload) const {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (PendingTasks == 0)
      return true;
    DestroyRequested = true;
    PendingDestroyPayload = Payload;
    return false;
  }

private:
  mutable int PendingTasks = 0;
  mutable bool DestroyRequested = false;
  mutable PayloadT PendingDestroyPayload{};
  mutable std::mutex Mutex;
};

} // namespace feme::execution

#endif // FEME_EXECUTION_DEFERREDDESTROYREFCOUNTED_H
