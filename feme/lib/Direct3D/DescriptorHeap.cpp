//===- DescriptorHeap.cpp - D3D descriptor-heap plan ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/DescriptorHeap.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateDescriptorHeapPlan(const DescriptorHeapPlan &Plan) {
  if (Plan.NumDescriptors == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "descriptor heap must not be empty");

  for (const DescriptorHeapRange &Allocation : Plan.Allocations) {
    if (Allocation.NumDescriptors == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "descriptor allocation must not be empty");
    uint64_t End = static_cast<uint64_t>(Allocation.BaseDescriptor) +
                   static_cast<uint64_t>(Allocation.NumDescriptors);
    if (End > Plan.NumDescriptors)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "descriptor allocation exceeds heap capacity");
  }
  return llvm::Error::success();
}

llvm::Error validateDescriptorCopy(const DescriptorHeapPlan &Source,
                                   uint32_t SourceOffset,
                                   const DescriptorHeapPlan &Destination,
                                   uint32_t DestinationOffset, uint32_t Count) {
  if (Source.Type != Destination.Type)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "descriptor copy must preserve heap type");
  uint64_t SourceEnd =
      static_cast<uint64_t>(SourceOffset) + static_cast<uint64_t>(Count);
  uint64_t DestinationEnd =
      static_cast<uint64_t>(DestinationOffset) + static_cast<uint64_t>(Count);
  if (SourceEnd > Source.NumDescriptors)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "descriptor copy exceeds source heap");
  if (DestinationEnd > Destination.NumDescriptors)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "descriptor copy exceeds destination heap");
  return llvm::Error::success();
}

} // namespace feme::direct3d
