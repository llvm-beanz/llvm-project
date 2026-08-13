//===- BarrierCalls.cpp - raised barrier intrinsic classification --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BarrierCalls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"

using namespace llvm;

namespace feme::cpu {

std::optional<MatchedBarrier> matchBarrierCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  switch (Callee->getIntrinsicID()) {
  case Intrinsic::dx_group_memory_barrier:
  case Intrinsic::spv_group_memory_barrier:
    return MatchedBarrier{BarrierMemoryScope::Group, false};
  case Intrinsic::dx_group_memory_barrier_with_group_sync:
  case Intrinsic::spv_group_memory_barrier_with_group_sync:
    return MatchedBarrier{BarrierMemoryScope::Group, true};
  case Intrinsic::dx_device_memory_barrier:
  case Intrinsic::spv_device_memory_barrier:
    return MatchedBarrier{BarrierMemoryScope::Device, false};
  case Intrinsic::dx_device_memory_barrier_with_group_sync:
  case Intrinsic::spv_device_memory_barrier_with_group_sync:
    return MatchedBarrier{BarrierMemoryScope::Device, true};
  case Intrinsic::dx_all_memory_barrier:
  case Intrinsic::spv_all_memory_barrier:
    return MatchedBarrier{BarrierMemoryScope::All, false};
  case Intrinsic::dx_all_memory_barrier_with_group_sync:
  case Intrinsic::spv_all_memory_barrier_with_group_sync:
    return MatchedBarrier{BarrierMemoryScope::All, true};
  default:
    return std::nullopt;
  }
}

} // namespace feme::cpu
