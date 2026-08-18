//===- QueryPool.cpp - VkQueryPool object model implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "QueryPool.h"
#include "Icd.h"

#include <cstring>

using namespace feme::vulkan;

namespace feme::vulkan {

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateQueryPool(VkDevice, const VkQueryPoolCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkQueryPool *pQueryPool) {
  // See QueryPool.h's file comment: only timestamp queries measure
  // anything this milestone's compute-only device can report truthfully.
  if (pCreateInfo->queryType != VK_QUERY_TYPE_TIMESTAMP)
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  QueryPool *Obj = Alloc.create<QueryPool>(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                           pCreateInfo->queryCount);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pQueryPool = toHandle<VkQueryPool>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
  if (!queryPool)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<QueryPool>(queryPool));
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetQueryPool(VkDevice, VkQueryPool queryPool, uint32_t firstQuery,
                 uint32_t queryCount) {
  fromHandle<QueryPool>(queryPool)->reset(firstQuery, queryCount);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount,
    size_t dataSize, void *pData, VkDeviceSize stride,
    VkQueryResultFlags flags) {
  auto *Pool = fromHandle<QueryPool>(queryPool);
  bool Is64Bit = (flags & VK_QUERY_RESULT_64_BIT) != 0;
  bool WithAvailability = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
  size_t ResultWidth = Is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != queryCount; ++I) {
    VkDeviceSize Offset = stride * I;
    if (Offset + ResultWidth * (WithAvailability ? 2 : 1) > dataSize)
      return VK_ERROR_INITIALIZATION_FAILED;
    auto *Dst = static_cast<uint8_t *>(pData) + Offset;
    bool Available = Pool->isAvailable(firstQuery + I);
    // Every query this milestone ever produces reports zero (see
    // QueryPool.h's file comment) -- available or not, there is no other
    // value to write.
    if (Available || (flags & VK_QUERY_RESULT_PARTIAL_BIT) != 0 ||
        (flags & VK_QUERY_RESULT_WAIT_BIT) == 0) {
      if (Is64Bit) {
        uint64_t Value = 0;
        std::memcpy(Dst, &Value, sizeof(Value));
      } else {
        uint32_t Value32 = 0;
        std::memcpy(Dst, &Value32, sizeof(Value32));
      }
    }
    if (WithAvailability) {
      auto *AvailDst = Dst + ResultWidth;
      if (Is64Bit) {
        uint64_t AvailFlag = Available ? 1 : 0;
        std::memcpy(AvailDst, &AvailFlag, sizeof(AvailFlag));
      } else {
        uint32_t AvailFlag32 = Available ? 1 : 0;
        std::memcpy(AvailDst, &AvailFlag32, sizeof(AvailFlag32));
      }
    }
    if (!Available && Result == VK_SUCCESS)
      // Every query this ICD's synchronous execution model could ever
      // resolve is already resolved by the time this runs (see Sync.h's
      // file comment for the same reasoning applied to fences/
      // semaphores/events): an unavailable query stays unavailable
      // regardless of `VK_QUERY_RESULT_WAIT_BIT`, since no further work is
      // ever pending to eventually write it.
      Result = VK_NOT_READY;
  }
  return Result;
}

} // namespace feme::vulkan
