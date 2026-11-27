#ifndef FEME_DIRECT3D_ACCELERATIONSTRUCTUREPLAN_H
#define FEME_DIRECT3D_ACCELERATIONSTRUCTUREPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

enum class AccelerationStructureKind : uint32_t {
  BottomLevel = 0,
  TopLevel = 1,
};

struct AccelerationStructurePlan {
  AccelerationStructureKind Kind = AccelerationStructureKind::BottomLevel;
  uint64_t SizeBytes = 0;
  uint64_t OffsetBytes = 0;
  uint32_t GeometryCount = 0;
  uint32_t InstanceCount = 0;
  bool AllowUpdate = false;
  bool AllowCompact = false;
};

llvm::Error
validateAccelerationStructurePlan(const AccelerationStructurePlan &Plan);

} // namespace feme::direct3d

#endif
