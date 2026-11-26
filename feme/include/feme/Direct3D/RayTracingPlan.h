#ifndef FEME_DIRECT3D_RAYTRACINGPLAN_H
#define FEME_DIRECT3D_RAYTRACINGPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct RayTracingPlan {
  uint32_t MaxPayloadBytes = 0;
  uint32_t MaxAttributeBytes = 0;
  uint32_t MaxRecursionDepth = 0;
  uint32_t MaxInstances = 0;
  uint32_t MaxGeometries = 0;
};

llvm::Error validateRayTracingPlan(const RayTracingPlan &Plan);

} // namespace feme::direct3d

#endif
