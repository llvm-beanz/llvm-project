//===- ComputePlan.cpp - D3D headless compute plan --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/ComputePlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateDevicePlan(const DevicePlan &Plan) {
  if (!Plan.ComputeQueue)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "headless compute requires a compute queue");
  if (Plan.RequestedWaveSize != 0 &&
      (Plan.RequestedWaveSize < 4 ||
       (Plan.RequestedWaveSize & (Plan.RequestedWaveSize - 1)) != 0))
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "requested wave size must be zero or a power of two >= 4");
  if (Plan.RequestedWaveSize > 64)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "requested wave size exceeds the bounded CPU wave policy");
  if (Plan.RepeatDispatches == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "repeat dispatch count must be positive");
  return llvm::Error::success();
}

llvm::Error validateComputeDispatchPlan(const ComputeDispatchPlan &Plan) {
  for (uint32_t Size : Plan.GroupSize) {
    if (Size == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "thread-group dimensions must be positive");
  }
  for (uint32_t Count : Plan.GroupCount) {
    if (Count == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "dispatch dimensions must be positive");
  }
  if (Plan.WaveSize == 0 || (Plan.WaveSize & (Plan.WaveSize - 1)) != 0 ||
      Plan.WaveSize < 4 || Plan.WaveSize > 64)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "compute wave size must be a power of two from 4 through 64");
  if (Plan.RepeatCount == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "repeat count must be positive");
  if (!totalComputeThreads(Plan))
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "compute dispatch thread total overflow");
  return llvm::Error::success();
}

std::optional<uint64_t> totalComputeThreads(const ComputeDispatchPlan &Plan) {
  uint64_t Threads = 1;
  for (uint32_t Size : Plan.GroupSize) {
    if (Size != 0 && Threads > UINT64_MAX / Size)
      return std::nullopt;
    Threads *= Size;
  }
  for (uint32_t Count : Plan.GroupCount) {
    if (Count != 0 && Threads > UINT64_MAX / Count)
      return std::nullopt;
    Threads *= Count;
  }
  if (Threads > UINT64_MAX / Plan.RepeatCount)
    return std::nullopt;
  return Threads * Plan.RepeatCount;
}

std::optional<uint64_t>
ceilComputeThreadsByWave(const ComputeDispatchPlan &Plan) {
  std::optional<uint64_t> Total = totalComputeThreads(Plan);
  if (!Total)
    return std::nullopt;
  if (Plan.WaveSize == 0)
    return std::nullopt;
  uint64_t Wave = Plan.WaveSize;
  if (*Total > UINT64_MAX - (Wave - 1))
    return std::nullopt;
  return (*Total + Wave - 1) / Wave;
}

} // namespace feme::direct3d
