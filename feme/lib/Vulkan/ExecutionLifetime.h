//===- ExecutionLifetime.h - Deferred-destroy keep-alive mixin --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `PendingExecutionRefCounted`: generalizes `Fence`'s own retain/release-
// with-deferred-destroy pattern (roadmap L398, `Sync.h`) to any object type
// a `QueueExecutor` task might still be reading when `vkDestroy*` is called
// for it (roadmap L409).
//
// Since the async `QueueExecutor` redesign (roadmap L228(h)/(i)),
// `vkQueueSubmit`/`vkQueueSubmit2` only enqueue a submission's work onto a
// queue's own worker thread and return immediately; the command buffers it
// references are only actually executed later, on that thread. Every
// `vkCmd*` recording call captures the resources it references (a bound
// `Pipeline`, a `vkCmdBeginRenderPass`'s `RenderPass`/`Framebuffer`, ...) as
// a raw pointer. The Vulkan spec lets an application legally destroy any
// such resource as soon as the submission *itself* has been enqueued --
// the application only has to keep the *command buffer* object alive until
// its submission completes, not the resources recorded into it, since a
// well-behaved driver keeps those alive on its own for exactly as long as
// an in-flight submission might still touch them.
//
// A type that mixes in `PendingExecutionRefCounted` and is retained
// (`retainForExecution`) by every `QueueExecutor` task that references it
// before that task is enqueued, and released (`releaseAndTakeDeferredDestroy`)
// once that task finishes running, gets this guarantee for free: its own
// `vkDestroy*` entry point calls `requestDestroy` instead of freeing the
// object directly, which defers the actual free until every such task has
// released it, exactly mirroring `Fence`'s existing pattern.
//
// Applied so far to `Pipeline`, `RenderPass`, and `Framebuffer` (`L409`'s
// own standalone reproducer, `leakprobe_uaf.cpp`, directly proved these
// three vulnerable), and to `Buffer`, `Image`, `ImageView`, `DescriptorSet`,
// `BufferView`, `Sampler`, `Event`, and `QueryPool` (`L410`'s follow-up
// audit: referenced by recorded commands the exact same way -- a raw
// pointer, no keep-alive -- and confirmed subject to the identical race).
// This closes `L410`'s full six-type follow-up list; see `Roadmap.md`'s
// `L410` entry.
//
// Also applied to `DeviceMemory` (`L421`): `Image`/`Buffer` only hold a
// raw, non-owning `DeviceMemory *` (`MemoryRangeBinding`), so being
// themselves `PendingExecutionRefCounted` does not stop `vkFreeMemory`
// from freeing their *bound memory* out from under an in-flight
// `QueueExecutor` task -- the identical race, one level deeper. See
// `Roadmap.md`'s `L421` entry.
//
// Also applied to `CommandBuffer` itself (`L422`): every type above is a
// resource a recorded command merely *references*, kept alive opposite a
// `vkDestroy*` call on *that* resource -- but `executeCommandBuffer`
// (`CommandBuffer.cpp`) takes a `const CommandBuffer &` and reads
// `CmdBuf.commands()` live on the `QueueExecutor` worker thread, well
// after `vkQueueSubmit` enqueued the task and returned. The spec lets an
// app free a command buffer (`vkFreeCommandBuffers`/
// `vkDestroyCommandPool`) the moment *its own* submission is known
// complete; a (fully legal) CTS pattern of tearing every per-test object
// down together right after a `vkWaitForFences` timeout, with no further
// wait, raced this same read -- the identical mechanism, one level up
// from every resource a command buffer records. See `Roadmap.md`'s `L422`
// entry.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_EXECUTIONLIFETIME_H
#define FEME_LIB_VULKAN_EXECUTIONLIFETIME_H

#include <vulkan/vulkan_core.h>

#include <mutex>

namespace feme::vulkan {

/// See the file comment. All public methods are safe to call from any
/// thread (the calling host thread doing a `vkDestroy*`, or a
/// `QueueExecutor` worker thread finishing a task), and are `const` so
/// they remain usable through a `const Pipeline *`/`const RenderPass *`/
/// etc. (recorded commands reference several of these types that way).
class PendingExecutionRefCounted {
public:
  /// Called once per `QueueExecutor` task that references this object,
  /// synchronously on the submitting host thread, before that task is
  /// enqueued (`vkQueueSubmit`/`vkQueueSubmit2`, `Sync.cpp`) -- this must
  /// happen before any other thread can possibly call this object's own
  /// `vkDestroy*`, for the same reason `Fence::retainForPendingSignal`'s
  /// own comment (`Sync.h`) gives.
  void retainForExecution() const {
    std::lock_guard<std::mutex> Lock(Mutex);
    ++PendingTasks;
  }

  /// Called by a `QueueExecutor` task once it finishes running (whether it
  /// succeeded, failed, or skipped work because the device was already
  /// lost). If a `vkDestroy*` call already asked to destroy this object
  /// while this was the last outstanding task (`requestDestroy` below
  /// returned false), returns true and sets \p OutCallbacks to the
  /// `VkAllocationCallbacks *` that call was given -- the caller must then
  /// free this object with an `Allocator` built from that pointer.
  /// Otherwise returns false and leaves \p OutCallbacks untouched.
  bool releaseAndTakeDeferredDestroy(
      const VkAllocationCallbacks *&OutCallbacks) const {
    std::lock_guard<std::mutex> Lock(Mutex);
    --PendingTasks;
    if (PendingTasks != 0 || !DestroyRequested)
      return false;
    OutCallbacks = PendingDestroyCallbacks;
    return true;
  }

  /// `vkDestroy*`: returns true if the caller may free this object
  /// immediately (the ordinary case: every `QueueExecutor` task that ever
  /// referenced it has already finished running). Returns false if at
  /// least one such task has not finished yet -- in that case this object
  /// defers its own destruction (remembering \p Callbacks) until that task
  /// releases it (`releaseAndTakeDeferredDestroy` above), instead of
  /// letting the caller free memory a `QueueExecutor` worker thread might
  /// still be reading.
  bool requestDestroy(const VkAllocationCallbacks *Callbacks) const {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (PendingTasks == 0)
      return true;
    DestroyRequested = true;
    PendingDestroyCallbacks = Callbacks;
    return false;
  }

private:
  mutable int PendingTasks = 0;
  mutable bool DestroyRequested = false;
  mutable const VkAllocationCallbacks *PendingDestroyCallbacks = nullptr;
  mutable std::mutex Mutex;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_EXECUTIONLIFETIME_H
