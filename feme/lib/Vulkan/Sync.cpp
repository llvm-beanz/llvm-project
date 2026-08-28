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

/// Consumes every wait in \p Waits, in order, returning the first failure
/// (see `Sync.h`'s file comment: a legally-ordered wait's semaphore is
/// already signaled by the time this synchronous ICD sees it -- an
/// unsignaled one is a real application ordering error).
VkResult consumeWaits(ArrayRef<SemaphoreOp> Waits) {
  for (const SemaphoreOp &Op : Waits) {
    if (Op.Sem->isTimeline()) {
      if (Op.Sem->timelineValue() < Op.Value)
        return VK_ERROR_INITIALIZATION_FAILED;
      continue;
    }
    if (!Op.Sem->waitAndConsumeBinary())
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

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice, VkFence fence) {
  return fromHandle<Fence>(fence)->isSignaled() ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice, uint32_t fenceCount,
                                               const VkFence *pFences,
                                               VkBool32 waitAll, uint64_t) {
  // Every fence is already in its final state by the time this is called
  // (see the file comment's synchronous `vkQueueSubmit` deviation), so
  // there is nothing to actually wait for: the result is knowable
  // immediately.
  bool Any = false, All = true;
  for (uint32_t I = 0; I != fenceCount; ++I) {
    bool S = fromHandle<Fence>(pFences[I])->isSignaled();
    Any |= S;
    All &= S;
  }
  return (waitAll ? All : Any) ? VK_SUCCESS : VK_TIMEOUT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue, uint32_t submitCount,
                                             const VkSubmitInfo *pSubmits,
                                             VkFence fence) {
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

    // Consume every wait semaphore before executing anything (see "Queues,
    // Scheduling, and Synchronization"): under this ICD's synchronous
    // execution model, a legally-ordered wait's semaphore is already
    // signaled by the time this runs (see Sync.h's file comment) -- an
    // unsignaled one here is a real application ordering error.
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
    if (VkResult R = consumeWaits(Waits); R != VK_SUCCESS)
      return R;

    std::vector<vulkan::CommandBuffer *> CmdBufs;
    CmdBufs.reserve(Submit.commandBufferCount);
    for (uint32_t J = 0; J != Submit.commandBufferCount; ++J)
      CmdBufs.push_back(fromHandle<CommandBuffer>(Submit.pCommandBuffers[J]));
    if (VkResult R = executeCommandBuffers(CmdBufs); R != VK_SUCCESS)
      return R;

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
    applySignals(Signals);
  }
  if (fence)
    fromHandle<Fence>(fence)->signal();
  return VK_SUCCESS;
}

// (Roadmap E3) `VK_KHR_synchronization2`'s `vkQueueSubmit2`: each wait/
// signal semaphore and command buffer arrives wrapped in its own
// `pNext`-extensible info struct instead of `vkQueueSubmit`'s parallel
// arrays (see `EntryPoints.h`'s declaration), but translates down to the
// identical `Fence`/`Semaphore`/`CommandBuffer` execution model above --
// the same "new entrypoint, old backing model" pattern roadmap C7 used for
// queue families.
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue, uint32_t submitCount,
                                              const VkSubmitInfo2 *pSubmits,
                                              VkFence fence) {
  for (uint32_t I = 0; I != submitCount; ++I) {
    const VkSubmitInfo2 &Submit = pSubmits[I];

    std::vector<SemaphoreOp> Waits;
    Waits.reserve(Submit.waitSemaphoreInfoCount);
    for (uint32_t J = 0; J != Submit.waitSemaphoreInfoCount; ++J) {
      const VkSemaphoreSubmitInfo &Info = Submit.pWaitSemaphoreInfos[J];
      Waits.push_back({fromHandle<Semaphore>(Info.semaphore), Info.value});
    }
    if (VkResult R = consumeWaits(Waits); R != VK_SUCCESS)
      return R;

    std::vector<vulkan::CommandBuffer *> CmdBufs;
    CmdBufs.reserve(Submit.commandBufferInfoCount);
    for (uint32_t J = 0; J != Submit.commandBufferInfoCount; ++J)
      CmdBufs.push_back(fromHandle<CommandBuffer>(
          Submit.pCommandBufferInfos[J].commandBuffer));
    if (VkResult R = executeCommandBuffers(CmdBufs); R != VK_SUCCESS)
      return R;

    std::vector<SemaphoreOp> Signals;
    Signals.reserve(Submit.signalSemaphoreInfoCount);
    for (uint32_t J = 0; J != Submit.signalSemaphoreInfoCount; ++J) {
      const VkSemaphoreSubmitInfo &Info = Submit.pSignalSemaphoreInfos[J];
      auto *Sem = fromHandle<Semaphore>(Info.semaphore);
      Signals.push_back({Sem, Sem->isTimeline() ? Info.value : 0});
    }
    applySignals(Signals);
  }
  if (fence)
    fromHandle<Fence>(fence)->signal();
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue) {
  // Every submission already ran to completion synchronously by the time
  // `vkQueueSubmit` returned (see the file comment).
  return VK_SUCCESS;
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
vkWaitSemaphores(VkDevice, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t) {
  // See the file comment: every semaphore this could observe is already in
  // its final state, so the wait either succeeds immediately or times out
  // immediately -- there is nothing to actually wait for.
  bool WaitAll = (pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT) == 0;
  bool Any = false, All = true;
  for (uint32_t I = 0; I != pWaitInfo->semaphoreCount; ++I) {
    bool Satisfied = fromHandle<Semaphore>(pWaitInfo->pSemaphores[I])
                         ->timelineValue() >= pWaitInfo->pValues[I];
    Any |= Satisfied;
    All &= Satisfied;
  }
  return (WaitAll ? All : Any) ? VK_SUCCESS : VK_TIMEOUT;
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
