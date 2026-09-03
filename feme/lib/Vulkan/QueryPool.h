//===- QueryPool.h - VkQueryPool object model -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V3 `VkQueryPool` object model (see "Command Buffers" in
// feme/docs/FeMeVulkanDesign.md: "Reset query pool, begin/end query, write
// timestamp, and copy query results"). `VK_QUERY_TYPE_TIMESTAMP`,
// `VK_QUERY_TYPE_OCCLUSION`, and (roadmap H9) `VK_QUERY_TYPE_PIPELINE_
// STATISTICS` are accepted at creation. Timestamp queries still report the
// value zero: this software device has no wall-clock timestamp counter to
// sample, and zero is a valid (if maximally coarse)
// `VkPhysicalDeviceLimits::timestampPeriod`-scaled value no application can
// mistake for a real measurement. Occlusion queries count the exact number
// of covered samples whose depth/stencil tests pass across draws executed
// between `vkCmdBeginQuery`/`vkCmdEndQuery`, reusing the V6 software
// rasterizer's real per-sample coverage and depth/stencil results.
// Pipeline-statistics queries (roadmap H9) likewise count real per-draw/
// per-dispatch quantities the V6 rasterizer and V7 compute dispatch path
// already compute along the way (see `Executor.cpp`'s `PreparedDraw::
// PipelineStatsCounters` and `CommandBuffer.cpp`'s `runDispatch`) -- not
// every one of the 11 `VkQueryPipelineStatisticFlagBits` is exact in every
// pipeline shape yet (geometry-/tessellation-shader-stage counters are less
// exhaustively covered by real CTS runs so far than the vertex/fragment
// path), but every bit's own value is a real, honestly-computed count, never
// a placeholder zero or an invented value -- see roadmap H9's own row for
// which sub-cases still want more CTS coverage.
//
// (Roadmap H2f) Under a multiview render pass instance, `vkCmdBeginQuery`/
// `vkCmdEndQuery` implicitly span one query index per set bit of the active
// subpass's view mask (the Vulkan spec's own multiview query rule), not the
// single index a non-multiview query uses -- `CommandBuffer.cpp`'s
// `runDraw`/`executeCommandsInto` compute that count from
// `GraphicsState::Binding.ViewMask` and pass it down to `begin`/
// `markAvailable` below, and attribute each view's own passed-sample count
// (not the sum across every rendered view) to its own query index via
// `accumulateOcclusionSamples`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_QUERYPOOL_H
#define FEME_LIB_VULKAN_QUERYPOOL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Error.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace feme::vulkan {

/// (roadmap H9) `VK_QUERY_TYPE_PIPELINE_STATISTICS`'s own ten
/// draw-scoped counters, one flat scalar per `VkQueryPipelineStatisticFlagBits`
/// bit, in the Vulkan spec's own "bit order, LSB first" indexing (index `I`
/// is bit `1 << I`) -- the exact order `vkGetQueryPoolResults` must write
/// however many of a pool's own `pipelineStatistics` bits are set. Kept
/// distinct from `feme::graphics::PreparedDraw::PipelineStatsCounters`
/// (a named-field struct `Executor.cpp` writes) since this file has no
/// dependency on the graphics library and only needs the flat, indexable
/// shape `accumulatePipelineStatistics` below reads from.
enum class PipelineStatisticIndex : uint32_t {
  InputAssemblyVertices = 0,
  InputAssemblyPrimitives = 1,
  VertexShaderInvocations = 2,
  GeometryShaderInvocations = 3,
  GeometryShaderPrimitives = 4,
  ClippingInvocations = 5,
  ClippingPrimitives = 6,
  FragmentShaderInvocations = 7,
  TessControlShaderPatches = 8,
  TessEvalShaderInvocations = 9,
  ComputeShaderInvocations = 10,
  Count = 11,
};

/// A `VkQueryPool`: `QueryCount` fixed-size slots of one `VkQueryType`, each
/// either unavailable (never written, or reset) or available with one 64-bit
/// value per `componentCount()` (1 for every query type except
/// `VK_QUERY_TYPE_PIPELINE_STATISTICS`, whose own result layout is one
/// value per set bit of its creation-time `pipelineStatistics`, in "bit
/// order, LSB first"). An occlusion or pipeline-statistics query may
/// additionally be active between `vkCmdBeginQuery` and `vkCmdEndQuery`,
/// accumulating passed-sample counts or per-bit statistics respectively.
class QueryPool {
public:
  QueryPool(uint32_t QueryCount, VkQueryType Type,
            VkQueryPipelineStatisticFlags PipelineStatistics = 0)
      : Type(Type), PipelineStatistics(PipelineStatistics),
        Available(QueryCount, false), Active(QueryCount, false),
        Values(QueryCount,
               std::vector<uint64_t>(
                   Type == VK_QUERY_TYPE_PIPELINE_STATISTICS
                       ? static_cast<size_t>(
                             llvm::popcount(PipelineStatistics))
                       : 1,
                   0)) {}

  uint32_t queryCount() const { return Available.size(); }
  VkQueryType queryType() const { return Type; }
  VkQueryPipelineStatisticFlags pipelineStatistics() const {
    return PipelineStatistics;
  }
  /// How many 64-bit values one query slot's own result occupies -- 1 for
  /// every query type except `VK_QUERY_TYPE_PIPELINE_STATISTICS`, which is
  /// `popcount(pipelineStatistics())`.
  uint32_t componentCount() const {
    return Values.empty() ? 1 : static_cast<uint32_t>(Values[0].size());
  }

  /// `vkCmdResetQueryPool`/`vkResetQueryPool`: marks
  /// `[FirstQuery, FirstQuery+QueryCount)` unavailable again and discards any
  /// in-flight accumulation for them.
  void reset(uint32_t FirstQuery, uint32_t QueryCount) {
    for (uint32_t I = 0; I != QueryCount && FirstQuery + I < Available.size();
         ++I) {
      Available[FirstQuery + I] = false;
      Active[FirstQuery + I] = false;
      std::fill(Values[FirstQuery + I].begin(), Values[FirstQuery + I].end(),
                0);
    }
  }

  /// `vkCmdBeginQuery`: starts a fresh occlusion-query accumulation.
  /// Inside a multiview render pass instance, a query spans \p ViewCount
  /// (> 1) consecutive query indices starting at \p Query -- one per set
  /// bit of the active subpass's view mask, per the Vulkan spec's own
  /// multiview query rule (see `QueryPool.h`'s file comment) -- rather
  /// than the single index a non-multiview `vkCmdBeginQuery` uses.
  void begin(uint32_t Query, uint32_t ViewCount = 1) {
    for (uint32_t I = 0; I != ViewCount && Query + I < Available.size(); ++I) {
      Available[Query + I] = false;
      Active[Query + I] = true;
      std::fill(Values[Query + I].begin(), Values[Query + I].end(), 0);
    }
  }

  /// `vkCmdEndQuery`/`vkCmdWriteTimestamp`: marks \p Query (and, under
  /// multiview, its following \p ViewCount-1 implicit indices -- see
  /// `begin`'s own comment) available.
  void markAvailable(uint32_t Query, uint32_t ViewCount = 1) {
    for (uint32_t I = 0; I != ViewCount && Query + I < Available.size(); ++I) {
      Active[Query + I] = false;
      Available[Query + I] = true;
    }
  }

  /// Adds \p Samples to occlusion query index \p Query alone -- the one
  /// specific per-view slot a multiview query's own view is accumulating
  /// into, unlike a non-multiview query's single, implicitly-"per-view"
  /// index. Distinct per-index accumulation (rather than broadcasting one
  /// combined total to every currently-active index) is required so each
  /// of a multiview query's `ViewCount` slots ends up with that view's own
  /// passed-sample count instead of every slot sharing the sum across all
  /// views.
  void accumulateOcclusionSamples(uint32_t Query, uint64_t Samples) {
    if (Type != VK_QUERY_TYPE_OCCLUSION || Samples == 0 ||
        Query >= Values.size())
      return;
    Values[Query][0] += Samples;
  }

  /// (roadmap H9) Adds one draw's/dispatch's own contribution -- indexed
  /// by `PipelineStatisticIndex`, the same flat, bit-order-independent
  /// shape regardless of which of this pool's own `pipelineStatistics`
  /// bits are actually set -- to pipeline-statistics query index \p Query,
  /// projecting down to only this pool's own requested bits (in "bit
  /// order, LSB first", per `vkGetQueryPoolResults`'s own result-layout
  /// rule for this query type) exactly as `vkCmdBeginQuery`/
  /// `vkCmdEndQuery`'s own scope may span several draws/dispatches, each
  /// summing its own contribution here in turn.
  void accumulatePipelineStatistics(
      uint32_t Query,
      const std::array<uint64_t, static_cast<size_t>(
                                      PipelineStatisticIndex::Count)>
          &Counters) {
    if (Type != VK_QUERY_TYPE_PIPELINE_STATISTICS || Query >= Values.size())
      return;
    uint32_t Slot = 0;
    for (uint32_t Bit = 0;
         Bit != static_cast<uint32_t>(PipelineStatisticIndex::Count); ++Bit)
      if (PipelineStatistics & (1u << Bit))
        Values[Query][Slot++] += Counters[Bit];
  }

  bool isAvailable(uint32_t Query) const {
    return Query < Available.size() && Available[Query];
  }

  /// The single 64-bit result an ordinary (non-pipeline-statistics) query
  /// holds -- callers writing a pipeline-statistics query's own
  /// `componentCount()`-many values use `values(Query)` instead.
  uint64_t value(uint32_t Query) const {
    return Query < Values.size() && !Values[Query].empty() ? Values[Query][0]
                                                            : 0;
  }

  /// Every one of query \p Query's own `componentCount()` result values,
  /// in `vkGetQueryPoolResults`'s own write order.
  llvm::ArrayRef<uint64_t> values(uint32_t Query) const {
    return Query < Values.size() ? llvm::ArrayRef<uint64_t>(Values[Query])
                                  : llvm::ArrayRef<uint64_t>();
  }


private:
  VkQueryType Type;
  VkQueryPipelineStatisticFlags PipelineStatistics;
  std::vector<bool> Available;
  std::vector<bool> Active;
  std::vector<std::vector<uint64_t>> Values;
};

/// Writes one query slot's own result -- \p Pool's `componentCount()`-many
/// 64-bit (or, if `!Is64Bit`, 32-bit) values, followed by a trailing
/// availability flag of the same width if \p WithAvailability -- into
/// `[Dst, Dst+entrySize())`, per `vkGetQueryPoolResults`'s own result-layout
/// rule (one value per query type, except `componentCount()`-many for
/// `VK_QUERY_TYPE_PIPELINE_STATISTICS`). Shared by `vkGetQueryPoolResults`
/// (`QueryPool.cpp`) and `vkCmdCopyQueryPoolResults`'s own execution
/// (`CommandBuffer.cpp`'s `runCopyQueryPoolResults`), which honor exactly
/// the same layout.
inline VkDeviceSize queryResultEntrySize(const QueryPool &Pool, bool Is64Bit,
                                         bool WithAvailability) {
  VkDeviceSize ResultWidth = Is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
  return ResultWidth * (Pool.componentCount() + (WithAvailability ? 1 : 0));
}

/// Writes one query's own result into \p Dst: `componentCount()`-many
/// values (see `queryResultEntrySize`'s own comment on that count), plus a
/// trailing availability flag if \p WithAvailability. \p WriteValues
/// gates only the value portion -- per spec, "no result values are
/// written to pData for queries that are in the unavailable state" unless
/// `VK_QUERY_RESULT_WAIT_BIT`/`VK_QUERY_RESULT_PARTIAL_BIT` is set (see
/// `vkGetQueryPoolResults`'s own call site), but "availability state is
/// still written to pData for those queries if
/// VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set" -- unconditionally, even
/// when \p WriteValues is false. `vkCmdCopyQueryPoolResults`'s own
/// execution (`CommandBuffer.cpp`'s `runCopyQueryPoolResults`) always
/// passes `WriteValues = true`, since that entry point has no WAIT_BIT/
/// PARTIAL_BIT-style "don't touch pData" case to honor.
inline void writeQueryResult(const QueryPool &Pool, uint32_t Query,
                             bool Is64Bit, bool WithAvailability,
                             uint8_t *Dst, bool WriteValues = true) {
  VkDeviceSize ResultWidth = Is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
  if (WriteValues) {
    llvm::ArrayRef<uint64_t> Values = Pool.values(Query);
    for (uint32_t C = 0; C != Pool.componentCount(); ++C) {
      uint64_t Value = C < Values.size() ? Values[C] : 0;
      uint8_t *Out = Dst + ResultWidth * C;
      if (Is64Bit)
        std::memcpy(Out, &Value, sizeof(Value));
      else {
        uint32_t Value32 = static_cast<uint32_t>(Value);
        std::memcpy(Out, &Value32, sizeof(Value32));
      }
    }
  }
  if (WithAvailability) {
    uint8_t *AvailDst = Dst + ResultWidth * Pool.componentCount();
    uint64_t AvailFlag = Pool.isAvailable(Query) ? 1 : 0;
    if (Is64Bit)
      std::memcpy(AvailDst, &AvailFlag, sizeof(AvailFlag));
    else {
      uint32_t AvailFlag32 = static_cast<uint32_t>(AvailFlag);
      std::memcpy(AvailDst, &AvailFlag32, sizeof(AvailFlag32));
    }
  }
}

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_QUERYPOOL_H
