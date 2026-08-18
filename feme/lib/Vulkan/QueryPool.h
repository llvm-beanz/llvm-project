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
// timestamp, and copy query results"). Only `VK_QUERY_TYPE_TIMESTAMP` is
// accepted at creation: occlusion and pipeline-statistics queries measure
// rasterization/shading work this milestone's compute-only device does not
// perform yet (graphics is V6+), so there is nothing truthful they could
// report. Every query this milestone produces reports the value zero --
// this software device has no wall-clock timestamp counter to sample, and
// zero is a valid (if maximally coarse) `VkPhysicalDeviceLimits::
// timestampPeriod`-scaled value no application can mistake for a real
// measurement, exactly like every other "declared but not yet meaningfully
// implemented" value elsewhere in this ICD (e.g. `vkCmdPipelineBarrier`'s
// join-only semantics).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_QUERYPOOL_H
#define FEME_LIB_VULKAN_QUERYPOOL_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::vulkan {

/// A `VkQueryPool`: `QueryCount` fixed-size slots, each either unavailable
/// (never written, or reset) or available with a value -- always zero, per
/// the file comment.
class QueryPool {
public:
  explicit QueryPool(uint32_t QueryCount) : Available(QueryCount, false) {}

  uint32_t queryCount() const { return Available.size(); }

  /// `vkCmdResetQueryPool`/`vkResetQueryPool`: marks
  /// `[FirstQuery, FirstQuery+QueryCount)` unavailable again.
  void reset(uint32_t FirstQuery, uint32_t QueryCount) {
    for (uint32_t I = 0; I != QueryCount && FirstQuery + I < Available.size();
        ++I)
      Available[FirstQuery + I] = false;
  }

  /// `vkCmdWriteTimestamp`/`vkCmdEndQuery`: marks \p Query available (see
  /// the file comment: the value is always zero, so there is nothing else
  /// to record).
  void markAvailable(uint32_t Query) {
    if (Query < Available.size())
      Available[Query] = true;
  }

  bool isAvailable(uint32_t Query) const {
    return Query < Available.size() && Available[Query];
  }

private:
  std::vector<bool> Available;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_QUERYPOOL_H
