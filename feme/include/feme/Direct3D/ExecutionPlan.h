//===- ExecutionPlan.h - D3D copy/fence execution plan ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_EXECUTIONPLAN_H
#define FEME_DIRECT3D_EXECUTIONPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::direct3d {

struct BufferCopyPlan {
  uint64_t SourceOffset = 0;
  uint64_t DestinationOffset = 0;
  uint64_t SourceSize = 0;
  uint64_t DestinationSize = 0;
  uint64_t CopySize = 0;
};

struct FencePlan {
  uint64_t InitialValue = 0;
  uint64_t CompletedValue = 0;
  uint64_t TargetValue = 0;
  bool DecreasingValueAllowed = false;
};

struct ExecutionPlan {
  std::vector<BufferCopyPlan> Copies;
  FencePlan Fence;
  uint32_t RepeatCount = 1;
};

llvm::Error validateExecutionPlan(const ExecutionPlan &Plan);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_EXECUTIONPLAN_H
