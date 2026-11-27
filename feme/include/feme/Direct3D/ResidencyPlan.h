#ifndef FEME_DIRECT3D_RESIDENCYPLAN_H
#define FEME_DIRECT3D_RESIDENCYPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::direct3d {

enum class HeapKind : uint32_t {
  Default = 0,
  Upload = 1,
  Readback = 2,
  Custom = 3,
};

struct HeapPlan {
  HeapKind Kind = HeapKind::Default;
  uint64_t Size = 0;
  uint64_t Alignment = 65536;
  bool CpuVisible = false;
};

struct PlacedResourcePlan {
  uint64_t HeapSize = 0;
  uint64_t Offset = 0;
  uint64_t ResourceSize = 0;
  uint64_t Alignment = 0;
};

struct ResidencyPlan {
  std::vector<uint64_t> AllocationBytes;
  uint64_t BudgetBytes = 0;
  uint32_t MaxResidentAllocations = 0;
};

llvm::Error validateHeapPlan(const HeapPlan &Plan);
llvm::Error validatePlacedResourcePlan(const PlacedResourcePlan &Plan);
llvm::Error validateResidencyPlan(const ResidencyPlan &Plan);

} // namespace feme::direct3d

#endif
