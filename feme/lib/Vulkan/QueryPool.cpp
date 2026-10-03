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
      pCreateInfo->queryType != VK_QUERY_TYPE_PIPELINE_STATISTICS &&
      pCreateInfo->queryType != VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT)
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

  // (Roadmap L354) `VK_QUERY_RESULT_WAIT_BIT`: block the calling host
  // thread until every query in range is available, or a safety-net
  // timeout elapses, before sampling `isAvailable`/`values` below --
  // since roadmap L228(h)/(i), a query's ending command buffer may still
  // be executing on its own `VkQueue`'s `QueueExecutor` worker thread
  // when this host-thread call runs, so a single racy `isAvailable`
  // sample can observe "not yet available" even though the application
  // did everything right (submitted the ending command buffer, then
  // called this with WAIT_BIT, exactly the spec-legal idiom this bit
  // exists for) -- seen concretely as
  // `dEQP-VK.query_pool.occlusion_query.get_results_*_wait_query_*`
  // spuriously reporting `VK_NOT_READY` (and, worse, going on to
  // destroy the test's own render targets while that worker thread was
  // still using them, crashing in `ImageView::dimension()` via a
  // dangling `ImageView*`).
  if ((flags & VK_QUERY_RESULT_WAIT_BIT) != 0)
    Pool->waitAvailable(firstQuery, queryCount, getSafetyNetTimeoutNs());

  VkResult Result = VK_SUCCESS;
  for (uint32_t I = 0; I != queryCount; ++I) {
    VkDeviceSize Offset = stride * I;
    if (Offset + EntrySize > dataSize)
      return VK_ERROR_INITIALIZATION_FAILED;
    auto *Dst = static_cast<uint8_t *>(pData) + Offset;
    bool Available = Pool->isAvailable(firstQuery + I);
    // Per spec: "If VK_QUERY_RESULT_WAIT_BIT and VK_QUERY_RESULT_PARTIAL_BIT
    // are both not set then no result values are written to pData for
    // queries that are in the unavailable state" -- WAIT_BIT does not, by
    // itself, license writing a value; it only means the caller wants this
    // call to block until every query becomes available (see
    // `waitAvailable` above), which this query may still genuinely never
    // do (e.g. one that was reset but never begun/ended), in which case
    // it stays unavailable even after the wait above times out.
    bool WriteValues =
        Available || (flags & VK_QUERY_RESULT_PARTIAL_BIT) != 0;
    writeQueryResult(*Pool, firstQuery + I, Is64Bit, WithAvailability, Dst,
                     WriteValues);
    if (!Available && Result == VK_SUCCESS)
      // Still unavailable even after `waitAvailable`'s own safety-net
      // timeout above (if WAIT_BIT was set) -- either WAIT_BIT was never
      // requested, or this query is one that's never going to become
      // available on its own (see this function's own comment just
      // above).
      Result = VK_NOT_READY;
  }
  return Result;
}

} // namespace feme::vulkan
