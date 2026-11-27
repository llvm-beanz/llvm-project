#include "feme/Direct3D/ResidencyPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateHeapPlan(const HeapPlan &Plan) {
  if (Plan.Size == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "heap size must be positive");
  if (Plan.Alignment == 0 || (Plan.Alignment & (Plan.Alignment - 1)) != 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "heap alignment must be a power of two");
  if (Plan.Size % Plan.Alignment != 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "heap size must be alignment-multiple");
  if (Plan.Kind == HeapKind::Readback && !Plan.CpuVisible)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "readback heap must be CPU-visible");
  if (Plan.Kind == HeapKind::Upload && !Plan.CpuVisible)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "upload heap must be CPU-visible");
  return llvm::Error::success();
}

llvm::Error validatePlacedResourcePlan(const PlacedResourcePlan &Plan) {
  if (Plan.Alignment == 0 || (Plan.Alignment & (Plan.Alignment - 1)) != 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "placed resource alignment must be a power of two");
  if (Plan.Offset % Plan.Alignment != 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "placed resource offset is misaligned");
  if (Plan.ResourceSize == 0 || Plan.ResourceSize > Plan.HeapSize ||
      Plan.Offset > Plan.HeapSize - Plan.ResourceSize)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "placed resource exceeds heap bounds");
  return llvm::Error::success();
}

llvm::Error validateResidencyPlan(const ResidencyPlan &Plan) {
  uint64_t Total = 0;
  for (uint64_t Bytes : Plan.AllocationBytes) {
    if (Bytes == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "resident allocation must have positive size");
    if (Total > UINT64_MAX - Bytes)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "resident allocation total overflow");
    Total += Bytes;
  }
  if (Plan.BudgetBytes != 0 && Total > Plan.BudgetBytes)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "resident allocations exceed budget");
  if (Plan.MaxResidentAllocations != 0 &&
      Plan.AllocationBytes.size() > Plan.MaxResidentAllocations)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "resident allocation count exceeds limit");
  return llvm::Error::success();
}

} // namespace feme::direct3d
