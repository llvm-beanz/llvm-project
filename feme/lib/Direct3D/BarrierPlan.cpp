//===- BarrierPlan.cpp - D3D dependency/barrier plan ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/BarrierPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateBarrierPlan(const std::vector<BarrierPlan> &Barriers) {
  for (const BarrierPlan &Barrier : Barriers) {
    const SubresourceRange &Range = Barrier.Range;
    if (Range.NumMips == 0 || Range.NumArrays == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "barrier subresource range must not be empty");
    if (Barrier.Kind == BarrierKind::Transition &&
        Barrier.Before == Barrier.After)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "transition barrier must change resource state");
    if (Barrier.Kind == BarrierKind::UAV &&
        (Barrier.Before != ResourceState::UnorderedAccess ||
         Barrier.After != ResourceState::UnorderedAccess))
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "UAV barriers must retain the UAV state");
  }
  return llvm::Error::success();
}

} // namespace feme::direct3d
