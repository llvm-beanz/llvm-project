//===- DescriptorHeap.h - D3D descriptor-heap plan -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_DESCRIPTORHEAP_H
#define FEME_DIRECT3D_DESCRIPTORHEAP_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::direct3d {

enum class DescriptorHeapType : uint32_t {
  CBV_SRV_UAV = 0,
  Sampler = 1,
};

struct DescriptorHeapRange {
  uint32_t BaseDescriptor = 0;
  uint32_t NumDescriptors = 0;
};

struct DescriptorHeapPlan {
  DescriptorHeapType Type = DescriptorHeapType::CBV_SRV_UAV;
  uint32_t NumDescriptors = 0;
  std::vector<DescriptorHeapRange> Allocations;
};

llvm::Error validateDescriptorHeapPlan(const DescriptorHeapPlan &Plan);

llvm::Error validateDescriptorCopy(const DescriptorHeapPlan &Source,
                                   uint32_t SourceOffset,
                                   const DescriptorHeapPlan &Destination,
                                   uint32_t DestinationOffset, uint32_t Count);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_DESCRIPTORHEAP_H
