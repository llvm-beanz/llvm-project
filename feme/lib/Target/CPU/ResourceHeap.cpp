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

} // namespace feme::cpu
