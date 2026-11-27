//===- ExecutionPlan.cpp - D3D copy/fence execution plan --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/ExecutionPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateExecutionPlan(const ExecutionPlan &Plan) {
  if (Plan.RepeatCount == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "execution repeat count must be positive");
  if (Plan.Fence.CompletedValue > Plan.Fence.InitialValue &&
      Plan.Fence.CompletedValue > Plan.Fence.TargetValue)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "fence completed value advances past the target");
  if (Plan.Fence.TargetValue < Plan.Fence.InitialValue &&
      !Plan.Fence.DecreasingValueAllowed)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "decreasing fence target is not allowed");
  for (const BufferCopyPlan &Copy : Plan.Copies) {
    if (Copy.CopySize == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "copy size must be positive");
    if (Copy.SourceOffset > Copy.SourceSize ||
        Copy.CopySize > Copy.SourceSize - Copy.SourceOffset)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "buffer copy exceeds source range");
    if (Copy.DestinationOffset > Copy.DestinationSize ||
        Copy.CopySize > Copy.DestinationSize - Copy.DestinationOffset)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "buffer copy exceeds destination range");
  }
  return llvm::Error::success();
}

} // namespace feme::direct3d
