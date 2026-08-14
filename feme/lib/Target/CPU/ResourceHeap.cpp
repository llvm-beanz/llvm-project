//===- ResourceHeap.cpp - CPU target physical heap materialization ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/ResourceHeap.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>

using namespace llvm;

namespace feme::cpu {

std::vector<FemeDescriptor>
materializeResourceHeap(const ResourceInfo &Info,
                        ArrayRef<BoundResourceBinding> Bindings,
                        ArrayRef<FemeDescriptor> DynamicHeap) {
  std::vector<FemeDescriptor> Heap(
      static_cast<size_t>(Info.ReservedResourceHeapSize) + DynamicHeap.size(),
      FemeDescriptor{});

  for (const BoundResourceRange &Range : Info.BoundRanges) {
    const BoundResourceBinding *Matched = nullptr;
    for (const BoundResourceBinding &Binding : Bindings)
      if (Binding.Space == Range.Space &&
          Binding.BaseRegister == Range.BaseRegister) {
        Matched = &Binding;
        break;
      }
    if (!Matched)
      continue; // Every slot in this range stays the zero descriptor.

    size_t NumToCopy =
        std::min<size_t>(Range.RangeSize, Matched->Descriptors.size());
    for (size_t J = 0; J != NumToCopy; ++J)
      Heap[Range.HeapBase + J] = Matched->Descriptors[J];
  }

  llvm::copy(DynamicHeap, Heap.begin() + Info.ReservedResourceHeapSize);
  return Heap;
}

void runDispatch(EntryPointFn EntryFn, const ResourceInfo &Info,
                 const DispatchResources &Resources,
                 std::array<uint32_t, 3> GroupCount) {
  std::vector<FemeDescriptor> PhysicalResourceHeap = materializeResourceHeap(
      Info, Resources.BoundResources, Resources.ResourceHeap);

  FemeDispatchArgs Args{};
  Args.ResourceHeap = PhysicalResourceHeap.data();
  Args.ResourceHeapCount = static_cast<uint32_t>(PhysicalResourceHeap.size());
  Args.SamplerHeap = Resources.SamplerHeap.data();
  Args.SamplerHeapCount = static_cast<uint32_t>(Resources.SamplerHeap.size());
  Args.RootConstants = Resources.RootConstants.data();
  Args.RootConstantSize = static_cast<uint32_t>(Resources.RootConstants.size());
  Args.GroupCount[0] = GroupCount[0];
  Args.GroupCount[1] = GroupCount[1];
  Args.GroupCount[2] = GroupCount[2];
  // Groupshared allocation: `feme::cpu::EntryWrapperPass` (milestone 9)
  // allocates a small `groupshared` declaration on its own stack, so most
  // groups need nothing from here. A shader declaring more than that
  // pass's `GroupSharedStackLimit` needs a real host-supplied buffer
  // neither caller of this function provides yet.
  Args.GroupShared = nullptr;

  for (uint32_t Z = 0; Z != GroupCount[2]; ++Z) {
    for (uint32_t Y = 0; Y != GroupCount[1]; ++Y) {
      for (uint32_t X = 0; X != GroupCount[0]; ++X) {
        Args.GroupID[0] = X;
        Args.GroupID[1] = Y;
        Args.GroupID[2] = Z;
        EntryFn(&Args);
      }
    }
  }
}

} // namespace feme::cpu
