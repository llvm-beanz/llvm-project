//===- AmplificationDispatch.cpp - Checked task-stage mesh dispatch ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/AmplificationDispatch.h"

using namespace feme::graphics;
using llvm::Error;
using llvm::Expected;

Expected<AmplificationDispatchQueue>
AmplificationDispatchQueue::create(std::array<uint32_t, 3> GroupCount,
                                   const AmplificationDispatchLimits &Limits) {
  for (uint32_t Dim = 0; Dim != 3; ++Dim)
    if (GroupCount[Dim] > Limits.MaxGroupCount[Dim])
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "EmitMeshTasksEXT group count[%u] (%u) exceeds "
          "maxMeshWorkGroupCount[%u] (%u)",
          Dim, GroupCount[Dim], Dim, Limits.MaxGroupCount[Dim]);

  uint64_t Total = static_cast<uint64_t>(GroupCount[0]) *
                   static_cast<uint64_t>(GroupCount[1]) *
                   static_cast<uint64_t>(GroupCount[2]);
  if (Total > Limits.MaxTotalGroupCount)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "EmitMeshTasksEXT total group count (%llu) exceeds "
        "maxMeshWorkGroupTotalCount (%u)",
        static_cast<unsigned long long>(Total), Limits.MaxTotalGroupCount);

  return AmplificationDispatchQueue(GroupCount);
}

uint64_t AmplificationDispatchQueue::size() const {
  return static_cast<uint64_t>(GroupCount[0]) *
         static_cast<uint64_t>(GroupCount[1]) *
         static_cast<uint64_t>(GroupCount[2]);
}

std::array<uint32_t, 3>
AmplificationDispatchQueue::getGroupID(uint64_t Index) const {
  std::array<uint32_t, 3> ID{};
  ID[0] = static_cast<uint32_t>(Index % GroupCount[0]);
  uint64_t Rest = Index / GroupCount[0];
  ID[1] = static_cast<uint32_t>(Rest % GroupCount[1]);
  ID[2] = static_cast<uint32_t>(Rest / GroupCount[1]);
  return ID;
}
