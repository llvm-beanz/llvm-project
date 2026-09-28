//===- Sync.cpp - VkFence and queue submission implementations ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Sync.h"
#include "CommandBuffer.h"
#include "Diagnostics.h"
#include "Icd.h"
#include "Objects.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <chrono>
#include <thread>

using namespace feme::vulkan;
using namespace llvm;

namespace {

/// One semaphore wait or signal operation, shared by `vkQueueSubmit` and
/// (roadmap E3) `vkQueueSubmit2`'s translation down to it: `Value` is only
/// meaningful for a timeline semaphore (unifying `vkQueueSubmit`'s split
/// `VkSubmitInfo`/`VkTimelineSemaphoreSubmitInfo` shape and
/// `vkQueueSubmit2`'s single `VkSemaphoreSubmitInfo::value` with each
/// other).
struct SemaphoreOp {
  Semaphore *Sem;
  uint64_t Value;
};

/// Clamps a caller-supplied timeout (as passed to `vkWaitSemaphores`'s
/// explicit \p timeout parameter, which may legally be `UINT64_MAX`) to
/// `SafetyNetTimeoutNs` (`Sync.h`).
uint64_t applyWaitSafetyNet(uint64_t RequestedTimeoutNs) {
  return std::min(RequestedTimeoutNs, SafetyNetTimeoutNs);
}

/// Consumes every wait in \p Waits, in order: both a timeline semaphore's
/// wait (`Semaphore::waitTimeline`) and, since roadmap L228(h)/(i), a
/// binary semaphore's (`Semaphore::waitAndConsumeBinary`) genuinely block
/// the calling thread (a `QueueExecutor` worker thread, not the host
/// thread that called `vkQueueSubmit` -- see `Sync.h`'s file comment):
/// the semaphore being waited on may be signaled by a *different* queue's
/// own worker thread, running concurrently with this one.
VkResult consumeWaits(ArrayRef<SemaphoreOp> Waits) {
  for (const SemaphoreOp &Op : Waits) {
    bool Reached = Op.Sem->isTimeline()
                       ? Op.Sem->waitTimeline(Op.Value, SafetyNetTimeoutNs)
                       : Op.Sem->waitAndConsumeBinary(SafetyNetTimeoutNs);
    if (!Reached)
      return VK_ERROR_INITIALIZATION_FAILED;
  }
  return VK_SUCCESS;
}

/// Applies every signal in \p Signals, in order.
void applySignals(ArrayRef<SemaphoreOp> Signals) {
  for (const SemaphoreOp &Op : Signals) {
    if (Op.Sem->isTimeline())
      Op.Sem->signalTimeline(Op.Value);
    else
      Op.Sem->signalBinary();
  }
}

/// Executes every command buffer in \p CommandBuffers, in order, returning
/// the first failure if any (shared by `vkQueueSubmit`/`vkQueueSubmit2`).
VkResult executeCommandBuffers(ArrayRef<CommandBuffer *> CmdBufs) {
  for (CommandBuffer *CmdBuf : CmdBufs) {
    if (Error E = executeCommandBuffer(*CmdBuf)) {
      logCreationFailure(std::move(E), "vkQueueSubmit");
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }
  return VK_SUCCESS;
}

/// (Roadmap L228(h)/(i)) Builds the closure a `QueueExecutor` task runs
/// for one submission: consume \p Waits, execute \p CmdBufs, then apply
/// \p Signals -- exactly the work `vkQueueSubmit`'s own loop body used to
/// do synchronously, now deferred onto the queue's own worker thread. Any
/// failure latches \p Dev lost (`Device::markLost`) rather than trying to
/// report it back through the `vkQueueSubmit` call that already returned
/// by the time this runs.
std::function<void()> makeSubmissionTask(Device &Dev,
                                         std::vector<SemaphoreOp> Waits,
                                         std::vector<CommandBuffer *> CmdBufs,
                                         std::vector<SemaphoreOp> Signals) {
  return [&Dev, Waits = std::move(Waits), CmdBufs = std::move(CmdBufs),
          Signals = std::move(Signals)]() mutable {
    if (Dev.isLost())
      return;
    if (consumeWaits(Waits) != VK_SUCCESS || executeCommandBuffers(CmdBufs) !=
                                                  VK_SUCCESS) {
      Dev.markLost();
      return;
    }
    applySignals(Signals);
  };
}

/// (Roadmap L228(h)/(i)) Builds the closure that signals \p Fence, run as
/// its own trailing task on the queue so it stays ordered after every
/// submission's own task above (including when `submitCount` is `0`,
/// which the spec says must still signal an already-provided fence).
std::function<void()> makeFenceSignalTask(Device &Dev, VkFence FenceHandle) {
  return [&Dev, FenceHandle]() {
    if (!Dev.isLost())
      fromHandle<Fence>(FenceHandle)->signal();
  };
}

} // namespace

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
  Allocator Alloc(pAllocator);
  Fence *Obj = Alloc.create<Fence>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
      (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pFence = toHandle<VkFence>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice, VkFence fence, const VkAllocationCallbacks *pAllocator) {
  if (!fence)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Fence>(fence));
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice, uint32_t fenceCount,
                                             const VkFence *pFences) {
  for (uint32_t I = 0; I != fenceCount; ++I)
    fromHandle<Fence>(pFences[I])->reset();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device,
                                                VkFence fence) {
  if (fromHandle<Device>(device)->isLost())
    return VK_ERROR_DEVICE_LOST;
  return fromHandle<Fence>(fence)->isSignaled() ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
               VkBool32 waitAll, uint64_t timeout) {
  // (Roadmap L228(h)/(i)) Genuinely blocks: a fence's `QueueExecutor`
  // task may not have run yet on its own queue's worker thread (see
  // `Sync.h`'s file comment). `applyWaitSafetyNet` clamps even a literal
  // `UINT64_MAX` ("wait forever") \p timeout, for the same reason
  // `vkWaitSemaphores` below does.
  timeout = applyWaitSafetyNet(timeout);
  Device *Dev = fromHandle<Device>(device);

  if (waitAll) {
    auto Deadline = std::chrono::steady_clock::now() +
                    std::chrono::nanoseconds(timeout);
    for (uint32_t I = 0; I != fenceCount; ++I) {
      if (Dev->isLost())
        return VK_ERROR_DEVICE_LOST;
      uint64_t Remaining =
          timeout == UINT64_MAX
              ? UINT64_MAX
              : static_cast<uint64_t>(std::max<int64_t>(
                    0, std::chrono::duration_cast<std::chrono::nanoseconds>(
                           Deadline - std::chrono::steady_clock::now())
                           .count()));
      if (!fromHandle<Fence>(pFences[I])->wait(Remaining))
        return VK_TIMEOUT;
    }
    return Dev->isLost() ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
  }

  // `waitAll == VK_FALSE`: succeeds as soon as *any one* fence is
  // signaled -- blocking on an arbitrary one first could wait long past a
  // different one's own, earlier completion, so this polls all of them
  // with a short retry interval instead (matching `vkWaitSemaphores`'s
  // own `VK_SEMAPHORE_WAIT_ANY_BIT` handling below).
  auto Deadline =
      std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);
  while (true) {
    if (Dev->isLost())
      return VK_ERROR_DEVICE_LOST;
    for (uint32_t I = 0; I != fenceCount; ++I)
      if (fromHandle<Fence>(pFences[I])->wait(0))
        return VK_SUCCESS;
    if (timeout != UINT64_MAX && std::chrono::steady_clock::now() >= Deadline)
      return VK_TIMEOUT;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue,
                                             uint32_t submitCount,
                                             const VkSubmitInfo *pSubmits,
                                             VkFence fence) {
  Queue *Q = fromHandle<Queue>(queue);
  Device &Dev = Q->getDevice();
  if (Dev.isLost())
    return VK_ERROR_DEVICE_LOST;

  // (Roadmap L228(h)/(i)) Every submission's own wait/execute/signal work
  // is now parsed here (reading only caller-supplied structs and
  // already-existing objects, so safe on the calling thread) but *run* on
  // `queue`'s own `QueueExecutor` -- see `Sync.h`'s file comment for why
  // this, rather than running it synchronously right here, is required
  // for `one_to_n`/`wait_before_signal`-shaped submission chains.
  for (uint32_t I = 0; I != submitCount; ++I) {
    const VkSubmitInfo &Submit = pSubmits[I];

    // V3: a `VkTimelineSemaphoreSubmitInfo` in `pNext` carries the
    // per-semaphore wait/signal values a timeline semaphore consumes;
    // absent entirely, every semaphore in this submission must be binary.
    const VkTimelineSemaphoreSubmitInfo *TimelineInfo = nullptr;
    for (const auto *Base =
             static_cast<const VkBaseInStructure *>(Submit.pNext);
         Base; Base = Base->pNext)
      if (Base->sType ==
          VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
        TimelineInfo =
            reinterpret_cast<const VkTimelineSemaphoreSubmitInfo *>(Base);
        break;
      }

    std::vector<SemaphoreOp> Waits;
    Waits.reserve(Submit.waitSemaphoreCount);
    for (uint32_t J = 0; J != Submit.waitSemaphoreCount; ++J) {
      auto *Sem = fromHandle<Semaphore>(Submit.pWaitSemaphores[J]);
      uint64_t Target =
          (TimelineInfo && J < TimelineInfo->waitSemaphoreValueCount)
              ? TimelineInfo->pWaitSemaphoreValues[J]
              : 0;
      Waits.push_back({Sem, Target});
    }

    std::vector<vulkan::CommandBuffer *> CmdBufs;
    CmdBufs.reserve(Submit.commandBufferCount);
    for (uint32_t J = 0; J != Submit.commandBufferCount; ++J)
      CmdBufs.push_back(fromHandle<CommandBuffer>(Submit.pCommandBuffers[J]));

    std::vector<SemaphoreOp> Signals;
    Signals.reserve(Submit.signalSemaphoreCount);
    for (uint32_t J = 0; J != Submit.signalSemaphoreCount; ++J) {
      auto *Sem = fromHandle<Semaphore>(Submit.pSignalSemaphores[J]);
      uint64_t NewValue =
          Sem->isTimeline()
              ? ((TimelineInfo && J < TimelineInfo->signalSemaphoreValueCount)
                     ? TimelineInfo->pSignalSemaphoreValues[J]
                     : Sem->timelineValue())
              : 0;
      Signals.push_back({Sem, NewValue});
    }

    Q->getExecutor().enqueue(makeSubmissionTask(
        Dev, std::move(Waits), std::move(CmdBufs), std::move(Signals)));
  }
  if (fence)
    Q->getExecutor().enqueue(makeFenceSignalTask(Dev, fence));
  return VK_SUCCESS;
}

// (Roadmap E3) `VK_KHR_synchronization2`'s `vkQueueSubmit2`: each wait/
// signal semaphore and command buffer arrives wrapped in its own
// `pNext`-extensible info struct instead of `vkQueueSubmit`'s parallel
// arrays (see `EntryPoints.h`'s declaration), but translates down to the
// identical `Fence`/`Semaphore`/`CommandBuffer` execution model above --
// the same "new entrypoint, old backing model" pattern roadmap C7 used for
// queue families.
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue,
                                              uint32_t submitCount,
                                              const VkSubmitInfo2 *pSubmits,
                                              VkFence fence) {
  Queue *Q = fromHandle<Queue>(queue);
  Device &Dev = Q->getDevice();
  if (Dev.isLost())
    return VK_ERROR_DEVICE_LOST;

  for (uint32_t I = 0; I != submitCount; ++I) {
    const VkSubmitInfo2 &Submit = pSubmits[I];

    std::vector<SemaphoreOp> Waits;
    Waits.reserve(Submit.waitSemaphoreInfoCount);
    for (uint32_t J = 0; J != Submit.waitSemaphoreInfoCount; ++J) {
      const VkSemaphoreSubmitInfo &Info = Submit.pWaitSemaphoreInfos[J];
      Waits.push_back({fromHandle<Semaphore>(Info.semaphore), Info.value});
    }

    std::vector<vulkan::CommandBuffer *> CmdBufs;
    CmdBufs.reserve(Submit.commandBufferInfoCount);
    for (uint32_t J = 0; J != Submit.commandBufferInfoCount; ++J)
      CmdBufs.push_back(fromHandle<CommandBuffer>(
          Submit.pCommandBufferInfos[J].commandBuffer));

    std::vector<SemaphoreOp> Signals;
    Signals.reserve(Submit.signalSemaphoreInfoCount);
    for (uint32_t J = 0; J != Submit.signalSemaphoreInfoCount; ++J) {
      const VkSemaphoreSubmitInfo &Info = Submit.pSignalSemaphoreInfos[J];
      auto *Sem = fromHandle<Semaphore>(Info.semaphore);
      Signals.push_back({Sem, Sem->isTimeline() ? Info.value : 0});
    }

    Q->getExecutor().enqueue(makeSubmissionTask(
        Dev, std::move(Waits), std::move(CmdBufs), std::move(Signals)));
  }
  if (fence)
    Q->getExecutor().enqueue(makeFenceSignalTask(Dev, fence));
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
  Queue *Q = fromHandle<Queue>(queue);
  if (Q->getDevice().isLost())
    return VK_ERROR_DEVICE_LOST;
  if (!Q->getExecutor().waitIdle())
    return VK_TIMEOUT;
  return Q->getDevice().isLost() ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(VkDevice, const VkSemaphoreCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkSemaphore *pSemaphore) {
  bool Timeline = false;
  uint64_t InitialValue = 0;
  for (const auto *Base =
           static_cast<const VkBaseInStructure *>(pCreateInfo->pNext);
       Base; Base = Base->pNext)
    if (Base->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
      const auto *TypeInfo =
          reinterpret_cast<const VkSemaphoreTypeCreateInfo *>(Base);
      Timeline = TypeInfo->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE;
      InitialValue = TypeInfo->initialValue;
      break;
    }

  Allocator Alloc(pAllocator);
  Semaphore *Obj = Alloc.create<Semaphore>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                           Timeline, InitialValue);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pSemaphore = toHandle<VkSemaphore>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice, VkSemaphore semaphore,
                   const VkAllocationCallbacks *pAllocator) {
  if (!semaphore)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Semaphore>(semaphore));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice,
                                                          VkSemaphore semaphore,
                                                          uint64_t *pValue) {
  *pValue = fromHandle<Semaphore>(semaphore)->timelineValue();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkWaitSemaphores(VkDevice, const VkSemaphoreWaitInfo *pWaitInfo,
                 uint64_t timeout) {
  // (Roadmap L228) Genuinely blocks: a real, concurrently running host
  // thread's own `vkSignalSemaphore` call, or a different queue's own
  // `QueueExecutor` worker thread's own signal (roadmap L228(h)/(i)), may
  // not have happened yet (`dEQP-VK.synchronization.timeline_semaphore.
  // host_host.*` exercises exactly this -- one host thread waiting here
  // for another host thread's own later `vkSignalSemaphore` call, no
  // device work involved at all).
  //
  // `applyWaitSafetyNet` clamps even a literal `UINT64_MAX` ("wait
  // forever") \p timeout to a bounded internal ceiling: some other,
  // unrelated FeMe bug that never actually signals the awaited value
  // would otherwise hang this call (and the whole process) forever rather
  // than surfacing as this call's own ordinary, spec-legal `VK_TIMEOUT`
  // return.
  timeout = applyWaitSafetyNet(timeout);

  // A single semaphore is the common case (and the only one where
  // `VK_SEMAPHORE_WAIT_ANY_BIT` and the default all-of behavior are
  // indistinguishable), so it blocks directly on that one semaphore's own
  // condition variable rather than the coarser multi-semaphore handling
  // below.
  if (pWaitInfo->semaphoreCount == 1) {
    bool Reached = fromHandle<Semaphore>(pWaitInfo->pSemaphores[0])
                       ->waitTimeline(pWaitInfo->pValues[0], timeout);
    return Reached ? VK_SUCCESS : VK_TIMEOUT;
  }


  bool WaitAll = (pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT) == 0;
  if (WaitAll) {
    // Blocking on each semaphore in turn is equivalent to waiting for all
    // of them together (a signaled timeline counter never un-signals), so
    // long as each one's own share of the overall \p timeout is tracked
    // rather than re-applying the full budget to every semaphore in turn.
    auto Deadline = std::chrono::steady_clock::now() +
                    std::chrono::nanoseconds(timeout);
    for (uint32_t I = 0; I != pWaitInfo->semaphoreCount; ++I) {
      uint64_t Remaining =
          timeout == UINT64_MAX
              ? UINT64_MAX
              : static_cast<uint64_t>(std::max<int64_t>(
                    0, std::chrono::duration_cast<std::chrono::nanoseconds>(
                           Deadline - std::chrono::steady_clock::now())
                           .count()));
      if (!fromHandle<Semaphore>(pWaitInfo->pSemaphores[I])
               ->waitTimeline(pWaitInfo->pValues[I], Remaining))
        return VK_TIMEOUT;
    }
    return VK_SUCCESS;
  }

  // `VK_SEMAPHORE_WAIT_ANY_BIT`: succeeds as soon as *any one* semaphore
  // reaches its target -- blocking on an arbitrary one first could wait
  // long past a different one's own, earlier completion, so this polls
  // all of them with a short retry interval instead.
  auto Deadline =
      std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);
  while (true) {
    for (uint32_t I = 0; I != pWaitInfo->semaphoreCount; ++I)
      if (fromHandle<Semaphore>(pWaitInfo->pSemaphores[I])
              ->waitTimeline(pWaitInfo->pValues[I], 0))
        return VK_SUCCESS;
    if (timeout != UINT64_MAX && std::chrono::steady_clock::now() >= Deadline)
      return VK_TIMEOUT;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

VKAPI_ATTR VkResult VKAPI_CALL
vkSignalSemaphore(VkDevice, const VkSemaphoreSignalInfo *pSignalInfo) {
  fromHandle<Semaphore>(pSignalInfo->semaphore)
      ->signalTimeline(pSignalInfo->value);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(VkDevice, const VkEventCreateInfo *,
             const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
  Allocator Alloc(pAllocator);
  Event *Obj = Alloc.create<Event>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pEvent = toHandle<VkEvent>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice, VkEvent event, const VkAllocationCallbacks *pAllocator) {
  if (!event)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<Event>(event));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice, VkEvent event) {
  return fromHandle<Event>(event)->isSignaled() ? VK_EVENT_SET
                                                : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice, VkEvent event) {
  fromHandle<Event>(event)->set();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice, VkEvent event) {
  fromHandle<Event>(event)->reset();
  return VK_SUCCESS;
}

} // namespace feme::vulkan

