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

PreparedDispatch::PreparedDispatch(std::vector<FemeDescriptor> ResourceHeap,
                                   ArrayRef<FemeDescriptor> SamplerHeap,
                                   ArrayRef<uint8_t> RootConstants,
                                   std::array<uint32_t, 3> GroupCount)
    : ResourceHeap(std::move(ResourceHeap)), SamplerHeap(SamplerHeap),
      RootConstants(RootConstants), GroupCount(GroupCount) {}

PreparedDispatch PreparedDispatch::create(const ResourceInfo &Info,
                                          const DispatchResources &Resources,
                                          std::array<uint32_t, 3> GroupCount) {
  return PreparedDispatch(materializeResourceHeap(Info, Resources.BoundResources,
                                                   Resources.ResourceHeap),
                          Resources.SamplerHeap, Resources.RootConstants,
                          GroupCount);
}

FemeDispatchArgs
PreparedDispatch::argsFor(std::array<uint32_t, 3> GroupID,
                          MutableArrayRef<uint8_t> GroupShared) const {
  FemeDispatchArgs Args{};
  Args.ResourceHeap = ResourceHeap.data();
  Args.ResourceHeapCount = static_cast<uint32_t>(ResourceHeap.size());
  Args.SamplerHeap = SamplerHeap.data();
  Args.SamplerHeapCount = static_cast<uint32_t>(SamplerHeap.size());
  Args.RootConstants = RootConstants.data();
  Args.RootConstantSize = static_cast<uint32_t>(RootConstants.size());
  Args.GroupCount[0] = GroupCount[0];
  Args.GroupCount[1] = GroupCount[1];
  Args.GroupCount[2] = GroupCount[2];
  Args.GroupID[0] = GroupID[0];
  Args.GroupID[1] = GroupID[1];
  Args.GroupID[2] = GroupID[2];
  // Groupshared allocation: `feme::cpu::EntryWrapperPass` (milestone 9)
  // allocates a small `groupshared` declaration on its own stack, so most
  // groups need nothing from here. A shader declaring more than that
  // pass's `GroupSharedStackLimit` needs a real host-supplied buffer no
  // caller of this function provides yet.
  Args.GroupShared = GroupShared.data();
  return Args;
}

void invokeGroup(EntryPointFn EntryFn, const PreparedDispatch &Prepared,
                 std::array<uint32_t, 3> GroupID,
                 MutableArrayRef<uint8_t> GroupShared) {
  FemeDispatchArgs Args = Prepared.argsFor(GroupID, GroupShared);
  EntryFn(&Args);
}

void runDispatch(EntryPointFn EntryFn, const ResourceInfo &Info,
                 const DispatchResources &Resources,
                 std::array<uint32_t, 3> GroupCount) {
  PreparedDispatch Prepared =
      PreparedDispatch::create(Info, Resources, GroupCount);

  for (uint32_t Z = 0; Z != GroupCount[2]; ++Z)
    for (uint32_t Y = 0; Y != GroupCount[1]; ++Y)
      for (uint32_t X = 0; X != GroupCount[0]; ++X)
        invokeGroup(EntryFn, Prepared, {X, Y, Z}, /*GroupShared=*/{});
}

} // namespace feme::cpu
