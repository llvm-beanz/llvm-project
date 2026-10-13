//===- Sync.cpp - VkFence and queue submission implementations ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Sync.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "Descriptor.h"
#include "Diagnostics.h"
#include "Icd.h"
#include "Image.h"
#include "Memory.h"
#include "Objects.h"
#include "PhaseTiming.h"
#include "Pipeline.h"
#include "QueryPool.h"
#include "RenderPass.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace feme::vulkan;
using namespace llvm;

namespace feme::vulkan {

/// Parses `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS` (see `Sync.h`'s own doc
/// comment): unset, empty, non-numeric, or non-positive all mean "use
/// the default", matching this codebase's usual permissive treatment of
/// a malformed opt-in environment variable (`FEME_VULKAN_LOG_CREATION_ERRORS`,
/// `Diagnostics.cpp`) -- a typo here should fall back quietly rather than
/// crash or misbehave.
uint64_t parseSafetyNetTimeoutMsEnv(const char *Env) {
  if (Env && *Env) {
    uint64_t Ms = 0;
    if (!StringRef(Env).getAsInteger(10, Ms) && Ms > 0)
      return Ms * 1'000'000ULL;
  }
  return DefaultSafetyNetTimeoutNs;
}

uint64_t getSafetyNetTimeoutNs() {
  static const uint64_t TimeoutNs = parseSafetyNetTimeoutMsEnv(
      std::getenv("FEME_VULKAN_SAFETY_NET_TIMEOUT_MS"));
  return TimeoutNs;
}

} // namespace feme::vulkan

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
/// `getSafetyNetTimeoutNs()` (`Sync.h`).
uint64_t applyWaitSafetyNet(uint64_t RequestedTimeoutNs) {
  return std::min(RequestedTimeoutNs, getSafetyNetTimeoutNs());
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
    uint64_t TimeoutNs = getSafetyNetTimeoutNs();
    bool Reached = Op.Sem->isTimeline()
                       ? Op.Sem->waitTimeline(Op.Value, TimeoutNs)
                       : Op.Sem->waitAndConsumeBinary(TimeoutNs);
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

/// (Roadmap L409/L410) Every `Pipeline`/`RenderPass`/`Framebuffer`/`Buffer`/
/// `Image`/`ImageView`/`DescriptorSet`/`BufferView`/`Sampler`/`Event`/
/// `QueryPool` a submission's command buffers (including, recursively, any
/// secondary command buffers `vkCmdExecuteCommands` interprets into them)
/// reference -- gathered by `collectExecutionRefs` synchronously on the
/// submitting host thread, before a submission's own `QueueExecutor` task
/// is enqueued, so each one can be retained (`retainExecutionRefs`) before
/// that task might start running on another thread, and released again
/// (`releaseExecutionRefs`) once it finishes, whether it actually executed
/// them or not. See `PendingExecutionRefCounted`'s own comment
/// (`ExecutionLifetime.h`) for why this is necessary: without it, a
/// `vkDestroyPipeline`/`vkDestroyRenderPass`/`vkDestroyFramebuffer`/
/// `vkDestroyBuffer`/`vkDestroyImage`/`vkDestroyImageView`/
/// `vkDestroyEvent`/`vkDestroyQueryPool` call made (fully legally) right
/// after `vkQueueSubmit` returns, with no wait at all, races this same
/// task's own still-pending read of that object.
struct ExecutionRefs {
  std::vector<Pipeline *> Pipelines;
  std::vector<const RenderPass *> RenderPasses;
  std::vector<const Framebuffer *> Framebuffers;
  std::vector<Buffer *> Buffers;
  std::vector<Image *> Images;
  std::vector<ImageView *> ImageViews;
  std::vector<DescriptorSet *> DescriptorSets;
  std::vector<BufferView *> BufferViews;
  std::vector<Sampler *> Samplers;
  std::vector<Event *> Events;
  std::vector<QueryPool *> QueryPools;
  std::vector<DeviceMemory *> DeviceMemories;
};

/// Records \p View (if non-null) into \p Refs, plus the `Image` it wraps
/// (`ImageView::image()`) -- an `ImageView` is a thin reference onto its
/// `Image`'s own storage, so retaining only the view and not the image it
/// points to would leave the same use-after-free race open one level down.
void collectImageViewRef(ImageView *View, ExecutionRefs &Refs) {
  if (!View)
    return;
  Refs.ImageViews.push_back(View);
  if (Image *Img = View->image())
    Refs.Images.push_back(Img);
}

/// (Roadmap L410) Retains \p Set itself, plus every `Buffer`/`BufferView`/
/// `ImageView`/`Image`/`Sampler` it currently has bound, walked snapshot-
/// style the same way `CommandBuffer.cpp`'s own `buildBoundResources`
/// reads them at execution time (`Set->bindingArray`/`imageBindingArray`,
/// both taking `Set`'s own `Mutex` -- see `DescriptorSet`'s class comment).
/// This is the "descriptor-indirect" half of `L410`: unlike every other
/// type `collectExecutionRefs` walks directly out of `RecordedCommand`'s
/// own fields, a descriptor set's bound objects are reachable only through
/// its own internal storage.
void collectDescriptorSetRefs(DescriptorSet *Set, ExecutionRefs &Refs) {
  if (!Set)
    return;
  Refs.DescriptorSets.push_back(Set);
  for (const DescriptorSetLayoutBinding &B : Set->getLayout().bindings()) {
    // An immutable-sampler binding's `Samp` is fixed for the layout's
    // whole lifetime (see `DescriptorSetLayoutBinding::ImmutableSamplers`'s
    // own comment) rather than re-supplied by every `write`, so it must be
    // retained from the layout's own list, not (only) from each bound
    // array element below.
    for (Sampler *Samp : B.ImmutableSamplers)
      if (Samp)
        Refs.Samplers.push_back(Samp);
    // Only one of `bindingArray`/`imageBindingArray` is ever non-empty for
    // a given binding number -- the two maps are mutually exclusive by
    // descriptor type, enforced in `DescriptorSet`'s own constructor --
    // so calling both unconditionally is simple and safe; the "wrong" one
    // just returns an empty vector.
    for (const DescriptorBufferBinding &Buf : Set->bindingArray(B.Binding)) {
      if (Buf.Buf)
        Refs.Buffers.push_back(Buf.Buf);
      if (Buf.View) {
        Refs.BufferViews.push_back(Buf.View);
        if (Buffer *Underlying = Buf.View->buffer())
          Refs.Buffers.push_back(Underlying);
      }
    }
    for (const DescriptorImageBinding &Img :
         Set->imageBindingArray(B.Binding)) {
      collectImageViewRef(Img.View, Refs);
      if (Img.Samp)
        Refs.Samplers.push_back(Img.Samp);
    }
  }
}

void collectExecutionRefs(const CommandBuffer &CmdBuf, ExecutionRefs &Refs) {
  for (const RecordedCommand &Cmd : CmdBuf.commands()) {
    if (Cmd.Pipeline)
      Refs.Pipelines.push_back(Cmd.Pipeline);
    if (Cmd.BeginPass)
      Refs.RenderPasses.push_back(Cmd.BeginPass);
    if (Cmd.BeginFramebuffer) {
      Refs.Framebuffers.push_back(Cmd.BeginFramebuffer);
      // (Roadmap L410) `Framebuffer` itself is already retained just
      // above, but that does not keep its own attachment `ImageView`s
      // (and the `Image`s they wrap) alive -- a classic `VkRenderPass`
      // instance reads them via `Cmd.BeginFramebuffer->attachments()` at
      // execution time (`buildRenderTargetBinding`), not through any
      // separately-retained reference.
      for (ImageView *View : Cmd.BeginFramebuffer->attachments())
        collectImageViewRef(View, Refs);
    }
    // (Roadmap C6) Imageless framebuffer's own per-instance attachment
    // views, when `BeginFramebuffer` carries none of its own (see
    // `BeginAttachments`'s own comment, `CommandBuffer.h`).
    for (ImageView *View : Cmd.BeginAttachments)
      collectImageViewRef(View, Refs);
    // (Roadmap L410) `vkCmdBeginRendering`'s own render-target binding,
    // built directly at record time with no render pass/framebuffer in
    // between (`RenderTargetBinding`, RenderPass.h) -- every attachment
    // view it names needs the same retain `BeginAttachments`/
    // `BeginFramebuffer` above get.
    for (const RenderTargetView &Color : Cmd.RenderingBinding.Colors) {
      collectImageViewRef(Color.View, Refs);
      collectImageViewRef(Color.ResolveView, Refs);
    }
    if (Cmd.RenderingBinding.Depth) {
      collectImageViewRef(Cmd.RenderingBinding.Depth->View, Refs);
      collectImageViewRef(Cmd.RenderingBinding.Depth->ResolveView, Refs);
    }
    if (Cmd.RenderingBinding.Stencil) {
      collectImageViewRef(Cmd.RenderingBinding.Stencil->View, Refs);
      collectImageViewRef(Cmd.RenderingBinding.Stencil->ResolveView, Refs);
    }
    for (ImageView *Input : Cmd.RenderingBinding.Inputs)
      collectImageViewRef(Input, Refs);
    // (Roadmap L410) Every `Image *` field `RecordedCommand` ever
    // populates (`CopyImage`/`BlitImage`/`ResolveImage`/
    // `CopyBufferToImage`/`CopyImageToBuffer`'s `SrcImage`/`DstImage`,
    // `ClearColorImage`/`ClearDepthStencilImage`'s `DstImage`), plus every
    // `vkCmdPipelineBarrier` image-layout-transition target.
    if (Cmd.SrcImage)
      Refs.Images.push_back(Cmd.SrcImage);
    if (Cmd.DstImage)
      Refs.Images.push_back(Cmd.DstImage);
    for (const ImageLayoutTransition &Barrier : Cmd.ImageBarriers)
      if (Barrier.Img)
        Refs.Images.push_back(Barrier.Img);
    // (Roadmap L410) Every `Buffer *` field `RecordedCommand` ever
    // populates, regardless of which `Kind` this command is: each field
    // is only ever non-null for the `Kind`(s) that actually use it (see
    // `RecordedCommand`'s own per-field comments, `CommandBuffer.h`), so
    // checking all of them unconditionally here, instead of switching on
    // `Cmd.Op`, needs no per-`Kind` maintenance as new command kinds are
    // added.
    if (Cmd.IndirectBuffer)
      Refs.Buffers.push_back(Cmd.IndirectBuffer);
    if (Cmd.CountBuffer)
      Refs.Buffers.push_back(Cmd.CountBuffer);
    if (Cmd.SrcBuffer)
      Refs.Buffers.push_back(Cmd.SrcBuffer);
    if (Cmd.DstBuffer)
      Refs.Buffers.push_back(Cmd.DstBuffer);
    for (Buffer *VB : Cmd.VertexBuffers)
      if (VB)
        Refs.Buffers.push_back(VB);
    for (Buffer *XB : Cmd.XfbBuffers)
      if (XB)
        Refs.Buffers.push_back(XB);
    // (Roadmap L410) `vkCmdBindDescriptorSets`' own bound sets -- see
    // `collectDescriptorSetRefs`'s own comment for why each set's bound
    // buffer/image/sampler objects need their own walk here, not just the
    // set itself.
    for (DescriptorSet *Set : Cmd.DescriptorSets)
      collectDescriptorSetRefs(Set, Refs);
    // (Roadmap L410) `vkCmdSetEvent`/`vkCmdResetEvent`/`vkCmdWaitEvents`
    // (and their `synchronization2` equivalents, which funnel into the
    // same `Cmd.Events` field -- see `Event`'s own class comment,
    // `Sync.h`).
    for (Event *Ev : Cmd.Events)
      if (Ev)
        Refs.Events.push_back(Ev);
    // (Roadmap L410) `vkCmdResetQueryPool`/`vkCmdBeginQuery`/
    // `vkCmdEndQuery`/`vkCmdWriteTimestamp`/`vkCmdCopyQueryPoolResults`
    // (and their `2`-suffixed equivalents) all populate the same single
    // `TargetQueryPool` field -- see its own comment, `CommandBuffer.h`.
    if (Cmd.TargetQueryPool)
      Refs.QueryPools.push_back(Cmd.TargetQueryPool);
    if (Cmd.Op == RecordedCommand::Kind::ExecuteCommands)
      for (const CommandBuffer *Secondary : Cmd.SecondaryBuffers)
        collectExecutionRefs(*Secondary, Refs);
  }
}

/// (Roadmap L421) Every `Image`/`Buffer` already collected above is
/// retained for as long as a `QueueExecutor` task might still read it, but
/// that does not keep the `DeviceMemory` it is bound to alive: both hold
/// only a raw, non-owning `DeviceMemory *` (`MemoryRangeBinding`,
/// `MemoryBinding.h`), and `vkFreeMemory` is fully legal immediately after
/// `vkQueueSubmit` returns, with no wait -- the same spec rule `L409`'s
/// own file comment explains for every other type. Called once, after
/// `Refs.Images`/`Refs.Buffers` are fully populated (including every
/// image/buffer reached indirectly through an `ImageView`/`DescriptorSet`),
/// so it needs no special-casing for how each one was reached.
void collectDeviceMemoryRefs(ExecutionRefs &Refs) {
  for (Image *Img : Refs.Images)
    for (const MemoryRangeBinding &Range : Img->ranges())
      if (Range.Memory)
        Refs.DeviceMemories.push_back(Range.Memory);
  for (Buffer *B : Refs.Buffers)
    for (const MemoryRangeBinding &Range : B->ranges())
      if (Range.Memory)
        Refs.DeviceMemories.push_back(Range.Memory);
}

ExecutionRefs collectExecutionRefs(ArrayRef<CommandBuffer *> CmdBufs) {
  ExecutionRefs Refs;
  for (const CommandBuffer *CmdBuf : CmdBufs)
    collectExecutionRefs(*CmdBuf, Refs);
  collectDeviceMemoryRefs(Refs);
  return Refs;
}

void retainExecutionRefs(const ExecutionRefs &Refs) {
  for (Pipeline *P : Refs.Pipelines)
    P->retainForExecution();
  for (const RenderPass *RP : Refs.RenderPasses)
    RP->retainForExecution();
  for (const Framebuffer *FB : Refs.Framebuffers)
    FB->retainForExecution();
  for (Buffer *B : Refs.Buffers)
    B->retainForExecution();
  for (Image *Img : Refs.Images)
    Img->retainForExecution();
  for (ImageView *View : Refs.ImageViews)
    View->retainForExecution();
  for (DescriptorSet *Set : Refs.DescriptorSets)
    Set->retainForExecution();
  for (BufferView *View : Refs.BufferViews)
    View->retainForExecution();
  for (Sampler *Samp : Refs.Samplers)
    Samp->retainForExecution();
  for (Event *Ev : Refs.Events)
    Ev->retainForExecution();
  for (QueryPool *QP : Refs.QueryPools)
    QP->retainForExecution();
  for (DeviceMemory *Mem : Refs.DeviceMemories)
    Mem->retainForExecution();
}

/// The release half of `retainExecutionRefs`: for each retained object,
/// finishes a `vkDestroy*` call this task's own enqueueing raced with (see
/// `PendingExecutionRefCounted::releaseAndTakeDeferredDestroy`'s own
/// comment), freeing it if so.
void releaseExecutionRefs(const ExecutionRefs &Refs) {
  const VkAllocationCallbacks *Callbacks = nullptr;
  for (Pipeline *P : Refs.Pipelines)
    if (P->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(P);
  for (ImageView *View : Refs.ImageViews)
    if (View->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(View);
  for (Image *Img : Refs.Images)
    if (Img->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(Img);
  for (Buffer *B : Refs.Buffers)
    if (B->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(B);
  for (const RenderPass *RP : Refs.RenderPasses)
    if (RP->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(const_cast<RenderPass *>(RP));
  for (const Framebuffer *FB : Refs.Framebuffers)
    if (FB->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(const_cast<Framebuffer *>(FB));
  for (BufferView *View : Refs.BufferViews)
    if (View->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(View);
  for (Sampler *Samp : Refs.Samplers)
    if (Samp->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(Samp);
  for (Event *Ev : Refs.Events)
    if (Ev->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(Ev);
  for (QueryPool *QP : Refs.QueryPools)
    if (QP->releaseAndTakeDeferredDestroy(Callbacks))
      Allocator(Callbacks).destroy(QP);
  // (Roadmap L410) Unlike every type above, `DescriptorSet` was never
  // allocated through the generic `Allocator` (see `DescriptorSet`'s own
  // class comment and `DescriptorPool::free`/`reset`, `Descriptor.cpp`) --
  // `Callbacks` is passed through purely for `requestDestroy`/
  // `releaseAndTakeDeferredDestroy`'s shared signature and is never
  // consulted for this type, so the actual free here is a plain `delete`,
  // matching how `DescriptorPool::destroyOrDeferSet` would otherwise have
  // freed it immediately had this task's retain not still been pending.
  for (DescriptorSet *Set : Refs.DescriptorSets)
    if (Set->releaseAndTakeDeferredDestroy(Callbacks))
      delete Set;
  // (Roadmap L421) `DeviceMemory`'s own deferred free must also release
  // its backing storage (`freeShareableDeviceMemory`), mirroring
  // `vkFreeMemory`'s own immediate-free path (`Memory.cpp`) -- unlike
  // every other type above, simply `Allocator(Callbacks).destroy(Mem)`
  // would leak the shareable-memory-object/mmap this object owns.
  for (DeviceMemory *Mem : Refs.DeviceMemories)
    if (Mem->releaseAndTakeDeferredDestroy(Callbacks)) {
      freeShareableDeviceMemory(Mem->data(),
                               static_cast<size_t>(Mem->size()),
                               Mem->shareableFd());
      Allocator(Callbacks).destroy(Mem);
    }
}

/// (Roadmap L228(h)/(i)) Builds the closure a `QueueExecutor` task runs
/// for one submission: consume \p Waits, execute \p CmdBufs, then apply
/// \p Signals -- exactly the work `vkQueueSubmit`'s own loop body used to
/// do synchronously, now deferred onto the queue's own worker thread. Any
/// failure latches \p Dev lost (`Device::markLost`) rather than trying to
/// report it back through the `vkQueueSubmit` call that already returned
/// by the time this runs.
///
/// (Roadmap L409) Every `Pipeline`/`RenderPass`/`Framebuffer` \p CmdBufs
/// references is retained here, synchronously, before the closure is ever
/// returned (i.e. before any other thread could possibly call this
/// submission's own task) -- see `ExecutionRefs`'s own comment above for
/// why -- and released again once the closure finishes running, on every
/// exit path (including an early return for an already-lost device).
std::function<void()> makeSubmissionTask(Device &Dev,
                                         std::vector<SemaphoreOp> Waits,
                                         std::vector<CommandBuffer *> CmdBufs,
                                         std::vector<SemaphoreOp> Signals) {
  ExecutionRefs Refs = collectExecutionRefs(CmdBufs);
  retainExecutionRefs(Refs);
  return [&Dev, Waits = std::move(Waits), CmdBufs = std::move(CmdBufs),
          Signals = std::move(Signals), Refs = std::move(Refs)]() mutable {
    auto ReleaseGuard = llvm::scope_exit([&Refs] { releaseExecutionRefs(Refs); });
    if (Dev.isLost())
      return;
    if (consumeWaits(Waits) != VK_SUCCESS) {
      Dev.markLost();
      return;
    }
    VkResult ExecResult;
    {
      // (Roadmap L418) Times the *entire* command buffer replay, not
      // just the copy/blit commands `ImageCopyExec` already covers, to
      // bisect whether a slow case's real cost is inside command
      // execution at all.
      feme::vulkan::ScopedPhaseTimer PhaseTimer(
          feme::vulkan::Phase::CommandBufferExec);
      ExecResult = executeCommandBuffers(CmdBufs);
    }
    if (ExecResult != VK_SUCCESS) {
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
///
/// (Roadmap L398) The caller must already have called
/// `Fence::retainForPendingSignal` on \p FenceHandle's underlying object
/// before enqueuing this closure -- matched here by
/// `releasePendingSignalAndTakeDeferredDestroy`, which finishes a
/// `vkDestroyFence` call this task's own enqueueing raced with (see that
/// method's comment, `Sync.h`).
std::function<void()> makeFenceSignalTask(Device &Dev, VkFence FenceHandle) {
  return [&Dev, FenceHandle]() {
    Fence *F = fromHandle<Fence>(FenceHandle);
    if (!Dev.isLost())
      F->signal();
    const VkAllocationCallbacks *DeferredCallbacks = nullptr;
    if (F->releasePendingSignalAndTakeDeferredDestroy(DeferredCallbacks))
      Allocator(DeferredCallbacks).destroy(F);
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
  Fence *F = fromHandle<Fence>(fence);
  // (Roadmap L398) If a `QueueExecutor` fence-signal task enqueued by
  // `vkQueueSubmit`/`vkQueueSubmit2` for this fence has not run yet,
  // `requestDestroy` defers freeing `F` until that task finishes instead
  // of freeing it here -- see its own comment (`Sync.h`) for why.
  if (!F->requestDestroy(pAllocator))
    return;
  Allocator(pAllocator).destroy(F);
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

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,
                                               uint32_t fenceCount,
                                               const VkFence *pFences,
                                               VkBool32 waitAll,
                                               uint64_t timeout) {
  // (Roadmap L228(h)/(i)) Genuinely blocks: a fence's `QueueExecutor`
  // task may not have run yet on its own queue's worker thread (see
  // `Sync.h`'s file comment). `applyWaitSafetyNet` clamps even a literal
  // `UINT64_MAX` ("wait forever") \p timeout, for the same reason
  // `vkWaitSemaphores` below does.
  // (Roadmap L418) Timed as its own phase: separates genuine blocked-on-
  // another-thread wait time from this same call's own, much cheaper
  // bookkeeping (the `isLost()`/deadline arithmetic below), for a slow
  // CTS case's own profiling breakdown.
  feme::vulkan::ScopedPhaseTimer PhaseTimer(feme::vulkan::Phase::FenceWait);
  timeout = applyWaitSafetyNet(timeout);
  Device *Dev = fromHandle<Device>(device);

  if (waitAll) {
    auto Deadline =
        std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);
    for (uint32_t I = 0; I != fenceCount; ++I) {
      Fence *F = fromHandle<Fence>(pFences[I]);
      // (Roadmap L234) Poll in `DeviceLostPollSliceNs` slices rather than
      // one single wait for the whole remaining timeout: device loss can
      // be latched by another queue's worker thread at any point during
      // this wait (see `DeviceLostPollSliceNs`'s own comment,
      // `Sync.h`), and checking `isLost()` only once, before the wait
      // starts, would let that race report a misleading `VK_TIMEOUT`
      // long after the device was actually already lost.
      while (true) {
        if (Dev->isLost())
          return VK_ERROR_DEVICE_LOST;
        uint64_t Remaining =
            timeout == UINT64_MAX
                ? UINT64_MAX
                : static_cast<uint64_t>(std::max<int64_t>(
                      0, std::chrono::duration_cast<std::chrono::nanoseconds>(
                             Deadline - std::chrono::steady_clock::now())
                             .count()));
        if (Remaining == 0)
          return VK_TIMEOUT;
        uint64_t Slice = std::min(Remaining, DeviceLostPollSliceNs);
        if (F->wait(Slice))
          break; // Signaled; move on to the next fence.
        // That slice's own wait timed out (either a real, whole-remainder
        // timeout, or just this poll granularity's own slice boundary) --
        // loop back to the top either way, so a device lost *during* that
        // slice is still caught by this loop's own `isLost()` check before
        // concluding it was a genuine `VK_TIMEOUT` (the `Remaining == 0`
        // check above, next iteration).
      }
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
      if (Base->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
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
  if (fence) {
    // (Roadmap L398) Retain before enqueuing, not after -- see
    // `Fence::retainForPendingSignal`'s own comment (`Sync.h`) for why
    // this must happen on this calling host thread, synchronously,
    // before any other thread can possibly call `vkDestroyFence` on
    // this same fence.
    fromHandle<Fence>(fence)->retainForPendingSignal();
    Q->getExecutor().enqueue(makeFenceSignalTask(Dev, fence));
  }
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
  if (fence) {
    // (Roadmap L398) Retain before enqueuing, not after -- see
    // `Fence::retainForPendingSignal`'s own comment (`Sync.h`) for why
    // this must happen on this calling host thread, synchronously,
    // before any other thread can possibly call `vkDestroyFence` on
    // this same fence.
    fromHandle<Fence>(fence)->retainForPendingSignal();
    Q->getExecutor().enqueue(makeFenceSignalTask(Dev, fence));
  }
  return VK_SUCCESS;
}

// (Roadmap L236(a)) `vkQueueBindSparse`: this ICD has no sparse-residency
// support at all (`sparseBinding`/`sparseResidency*` are all unconditional
// `VK_FALSE`, "Initial Non-Goals"), and `isValidImageShape` (Image.cpp)
// already rejects `VK_IMAGE_CREATE_SPARSE_*_BIT` at `vkCreateImage` time
// (and `isValidBufferCreateInfo`, Buffer.cpp, rejects any nonzero
// `VkBufferCreateInfo::flags`, so no live buffer can carry
// `VK_BUFFER_CREATE_SPARSE_BINDING_BIT` either) -- so no live
// `VkImage`/`VkBuffer` in this ICD can ever actually need a sparse memory
// bind, and a conformant application must not populate any
// `VkBindSparseInfo::p{Buffer,ImageOpaque,ImageBind}` array without
// `sparseBinding` enabled first (`VUID-vkQueueBindSparse-queue-04611` and
// its neighbors).
//
// (Roadmap H46) Even so, `pBufferBinds`/`pImageOpaqueBinds` are processed
// for real below via `Buffer`/`Image::bindSparseRange` rather than
// silently ignored, since both describe ordinary flat resource-space
// ranges `ResourceBindings::bindRange` already models exactly -- this is
// forward-looking groundwork for whichever future H46 step makes
// `sparseBinding` real, exercised today only by a caller that bypasses
// the (currently unreachable, per the VUIDs above) normal validation
// path. `pImageBinds` (`VkSparseImageMemoryBind`'s subresource + offset +
// extent, per-tile addressing) is deliberately still not processed: that
// needs a real sparse-image tile model this ICD does not have, a larger
// piece of work left for whichever future H46 step makes
// `sparseResidencyImage2D`/`3D` real.
//
// What *is* required regardless is `vkQueueBindSparse`'s ordinary queue
// submission-order and semaphore/fence synchronization contract (per
// spec, `bindInfoCount == 0` is explicitly legal and must still honor any
// `fence`) -- so each `VkBindSparseInfo`'s wait/signal semaphores (plus an
// optional chained `VkTimelineSemaphoreSubmitInfo`, mirroring
// `vkQueueSubmit`'s own identical pattern above) are threaded through the
// same `QueueExecutor`/`makeSubmissionTask` machinery with an empty
// command-buffer list, so this call still correctly orders against, and
// is waited on by, any other submission on the same queue.

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                  const VkBindSparseInfo *pBindInfo, VkFence fence) {
  Queue *Q = fromHandle<Queue>(queue);
  Device &Dev = Q->getDevice();
  if (Dev.isLost())
    return VK_ERROR_DEVICE_LOST;

  for (uint32_t I = 0; I != bindInfoCount; ++I) {
    const VkBindSparseInfo &Bind = pBindInfo[I];

    for (uint32_t J = 0; J != Bind.bufferBindCount; ++J) {
      const VkSparseBufferMemoryBindInfo &BufBind = Bind.pBufferBinds[J];
      Buffer *Buf = fromHandle<Buffer>(BufBind.buffer);
      for (uint32_t K = 0; K != BufBind.bindCount; ++K) {
        const VkSparseMemoryBind &SMB = BufBind.pBinds[K];
        Buf->bindSparseRange(
            SMB.resourceOffset,
            SMB.memory ? fromHandle<DeviceMemory>(SMB.memory) : nullptr,
            SMB.memoryOffset, SMB.size);
      }
    }

    for (uint32_t J = 0; J != Bind.imageOpaqueBindCount; ++J) {
      const VkSparseImageOpaqueMemoryBindInfo &ImgBind =
          Bind.pImageOpaqueBinds[J];
      Image *Img = fromHandle<Image>(ImgBind.image);
      for (uint32_t K = 0; K != ImgBind.bindCount; ++K) {
        const VkSparseMemoryBind &SMB = ImgBind.pBinds[K];
        Img->bindSparseRange(
            SMB.resourceOffset,
            SMB.memory ? fromHandle<DeviceMemory>(SMB.memory) : nullptr,
            SMB.memoryOffset, SMB.size);
      }
    }

    const VkTimelineSemaphoreSubmitInfo *TimelineInfo = nullptr;
    for (const auto *Base = static_cast<const VkBaseInStructure *>(Bind.pNext);
         Base; Base = Base->pNext)
      if (Base->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
        TimelineInfo =
            reinterpret_cast<const VkTimelineSemaphoreSubmitInfo *>(Base);
        break;
      }

    std::vector<SemaphoreOp> Waits;
    Waits.reserve(Bind.waitSemaphoreCount);
    for (uint32_t J = 0; J != Bind.waitSemaphoreCount; ++J) {
      auto *Sem = fromHandle<Semaphore>(Bind.pWaitSemaphores[J]);
      uint64_t Target =
          (TimelineInfo && J < TimelineInfo->waitSemaphoreValueCount)
              ? TimelineInfo->pWaitSemaphoreValues[J]
              : 0;
      Waits.push_back({Sem, Target});
    }

    std::vector<SemaphoreOp> Signals;
    Signals.reserve(Bind.signalSemaphoreCount);
    for (uint32_t J = 0; J != Bind.signalSemaphoreCount; ++J) {
      auto *Sem = fromHandle<Semaphore>(Bind.pSignalSemaphores[J]);
      uint64_t NewValue =
          Sem->isTimeline()
              ? ((TimelineInfo && J < TimelineInfo->signalSemaphoreValueCount)
                     ? TimelineInfo->pSignalSemaphoreValues[J]
                     : Sem->timelineValue())
              : 0;
      Signals.push_back({Sem, NewValue});
    }

    Q->getExecutor().enqueue(makeSubmissionTask(
        Dev, std::move(Waits), std::vector<vulkan::CommandBuffer *>(),
        std::move(Signals)));
  }
  if (fence) {
    fromHandle<Fence>(fence)->retainForPendingSignal();
    Q->getExecutor().enqueue(makeFenceSignalTask(Dev, fence));
  }
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

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice, const VkSemaphoreCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
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

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) {
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

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(
    VkDevice, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout) {
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
    auto Deadline =
        std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);
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
  Event *Ev = fromHandle<Event>(event);
  // (Roadmap L410) See `vkDestroyPipeline`'s identical comment
  // (`Pipeline.cpp`, `L409`) -- defers freeing `Ev` until every
  // `QueueExecutor` task that ever referenced it (a recorded
  // `vkCmdSetEvent`/`vkCmdResetEvent`/`vkCmdWaitEvents`) has finished
  // running.
  if (!Ev->requestDestroy(pAllocator))
    return;
  Allocator(pAllocator).destroy(Ev);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice, VkEvent event) {
  return fromHandle<Event>(event)->isSignaled() ? VK_EVENT_SET : VK_EVENT_RESET;
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
