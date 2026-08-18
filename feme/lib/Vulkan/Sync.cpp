//===- Sync.cpp - VkFence and queue submission implementations ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Sync.h"
#include "CommandBuffer.h"
#include "Icd.h"
#include "Objects.h"

#include "llvm/Support/Error.h"

using namespace feme::vulkan;
using namespace llvm;

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

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
             VkFence fence) {
  for (uint32_t I = 0; I != submitCount; ++I) {
    const VkSubmitInfo &Submit = pSubmits[I];
    // No `VkSemaphore` exists in the object model yet (see the file
    // comment); a well-formed application respecting it cannot have a
    // valid handle to wait on or signal.
    if (Submit.waitSemaphoreCount != 0 || Submit.signalSemaphoreCount != 0)
      return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t J = 0; J != Submit.commandBufferCount; ++J) {
      auto *CmdBuf = fromHandle<CommandBuffer>(Submit.pCommandBuffers[J]);
      if (Error E = executeCommandBuffer(*CmdBuf)) {
        consumeError(std::move(E));
        return VK_ERROR_INITIALIZATION_FAILED;
      }
    }
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

} // namespace feme::vulkan
