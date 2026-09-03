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

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice, const VkQueryPoolCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
  if (pCreateInfo->queryType != VK_QUERY_TYPE_TIMESTAMP &&
      pCreateInfo->queryType != VK_QUERY_TYPE_OCCLUSION &&
      pCreateInfo->queryType != VK_QUERY_TYPE_PIPELINE_STATISTICS)
    return VK_ERROR_INITIALIZATION_FAILED;
  // (roadmap H9) Every one of the 11 real `VkQueryPipelineStatisticFlagBits`
  // is backed by a real, honestly-computed counter (see `QueryPool.h`'s
  // file comment) -- `pipelineStatisticsQuery`'s own spec text places no
  // restriction on which bit combination an application may request once
  // the feature is enabled, so any combination within the defined mask is
  // accepted, matching this ICD's existing "trust the loader/validation
  // layers to reject anything this device does not actually advertise"
  // precedent (see `EntryPoints.cpp`'s `vkCreateDevice`, which likewise
  // never inspects `pEnabledFeatures` itself).
  constexpr VkQueryPipelineStatisticFlags AllStatisticsBits =
      (1u << static_cast<uint32_t>(PipelineStatisticIndex::Count)) - 1;
  if (pCreateInfo->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS &&
      (pCreateInfo->pipelineStatistics & ~AllStatisticsBits) != 0)
    return VK_ERROR_INITIALIZATION_FAILED;

  Allocator Alloc(pAllocator);
  QueryPool *Obj = Alloc.create<QueryPool>(
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pCreateInfo->queryCount,
      pCreateInfo->queryType, pCreateInfo->pipelineStatistics);
  if (!Obj)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *pQueryPool = toHandle<VkQueryPool>(Obj);
  return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator) {
  if (!queryPool)
    return;
  Allocator Alloc(pAllocator);
  Alloc.destroy(fromHandle<QueryPool>(queryPool));
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetQueryPool(VkDevice, VkQueryPool queryPool,
                                                uint32_t firstQuery,
                                                uint32_t queryCount) {
  fromHandle<QueryPool>(queryPool)->reset(firstQuery, queryCount);
  return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetQueryPoolResults(VkDevice, VkQueryPool queryPool, uint32_t firstQuery,
                      uint32_t queryCount, size_t dataSize, void *pData,
                      VkDeviceSize stride, VkQueryResultFlags flags) {
  auto *Pool = fromHandle<QueryPool>(queryPool);
  bool Is64Bit = (flags & VK_QUERY_RESULT_64_BIT) != 0;
  bool WithAvailability = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0;
  VkDeviceSize EntrySize = queryResultEntrySize(*Pool, Is64Bit, WithAvailability);

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != queryCount; ++I) {
    VkDeviceSize Offset = stride * I;
    if (Offset + EntrySize > dataSize)
      return VK_ERROR_INITIALIZATION_FAILED;
    auto *Dst = static_cast<uint8_t *>(pData) + Offset;
    bool Available = Pool->isAvailable(firstQuery + I);
    if (Available || (flags & VK_QUERY_RESULT_PARTIAL_BIT) != 0 ||
        (flags & VK_QUERY_RESULT_WAIT_BIT) == 0)
      writeQueryResult(*Pool, firstQuery + I, Is64Bit, WithAvailability, Dst);
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
