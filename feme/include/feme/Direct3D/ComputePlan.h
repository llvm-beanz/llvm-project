//===- ComputePlan.h - D3D headless compute plan ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_COMPUTEPLAN_H
#define FEME_DIRECT3D_COMPUTEPLAN_H

#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <optional>

namespace feme::direct3d {

enum class CommandQueueKind : uint32_t {
  Direct = 0,
  Compute = 1,
  Copy = 2,
};

struct DevicePlan {
  bool DirectQueue = true;
  bool ComputeQueue = true;
  bool CopyQueue = true;
  uint32_t RequestedWaveSize = 0;
  uint32_t RepeatDispatches = 1;
};

struct ComputeDispatchPlan {
  std::array<uint32_t, 3> GroupSize = {1, 1, 1};
  std::array<uint32_t, 3> GroupCount = {1, 1, 1};
  uint32_t WaveSize = 0;
  uint32_t RepeatCount = 1;
};

llvm::Error validateDevicePlan(const DevicePlan &Plan);
llvm::Error validateComputeDispatchPlan(const ComputeDispatchPlan &Plan);

std::optional<uint64_t> totalComputeThreads(const ComputeDispatchPlan &Plan);
std::optional<uint64_t>
ceilComputeThreadsByWave(const ComputeDispatchPlan &Plan);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_COMPUTEPLAN_H
