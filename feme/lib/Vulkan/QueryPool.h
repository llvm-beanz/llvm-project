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
// timestamp, and copy query results"). `VK_QUERY_TYPE_TIMESTAMP` and
// `VK_QUERY_TYPE_OCCLUSION` are accepted at creation. Timestamp queries still
// report the value zero: this software device has no wall-clock timestamp
// counter to sample, and zero is a valid (if maximally coarse)
// `VkPhysicalDeviceLimits::timestampPeriod`-scaled value no application can
// mistake for a real measurement. Occlusion queries, by contrast, now count
// the exact number of covered samples whose depth/stencil tests pass across
// draws executed between `vkCmdBeginQuery`/`vkCmdEndQuery`, reusing the V6
// software rasterizer's real per-sample coverage and depth/stencil results.
// Pipeline-statistics queries remain rejected: there is still no truthful
// counter for those yet.
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

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace feme::vulkan {

/// A `VkQueryPool`: `QueryCount` fixed-size slots of one `VkQueryType`, each
/// either unavailable (never written, or reset) or available with a 64-bit
/// value. An occlusion query may additionally be active between
/// `vkCmdBeginQuery` and `vkCmdEndQuery`, accumulating passed-sample counts.
class QueryPool {
public:
  QueryPool(uint32_t QueryCount, VkQueryType Type)
      : Type(Type), Available(QueryCount, false), Active(QueryCount, false),
        Values(QueryCount, 0) {}

  uint32_t queryCount() const { return Available.size(); }
  VkQueryType queryType() const { return Type; }

  /// `vkCmdResetQueryPool`/`vkResetQueryPool`: marks
  /// `[FirstQuery, FirstQuery+QueryCount)` unavailable again and discards any
  /// in-flight accumulation for them.
  void reset(uint32_t FirstQuery, uint32_t QueryCount) {
    for (uint32_t I = 0; I != QueryCount && FirstQuery + I < Available.size();
         ++I) {
      Available[FirstQuery + I] = false;
      Active[FirstQuery + I] = false;
      Values[FirstQuery + I] = 0;
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
      Values[Query + I] = 0;
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
    Values[Query] += Samples;
  }

  bool isAvailable(uint32_t Query) const {
    return Query < Available.size() && Available[Query];
  }

  uint64_t value(uint32_t Query) const {
    return Query < Values.size() ? Values[Query] : 0;
  }

private:
  VkQueryType Type;
  std::vector<bool> Available;
  std::vector<bool> Active;
  std::vector<uint64_t> Values;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_QUERYPOOL_H
