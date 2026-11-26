#include "feme/Direct3D/RayTracingPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateRayTracingPlan(const RayTracingPlan &Plan) {
  if (Plan.MaxPayloadBytes == 0 || Plan.MaxPayloadBytes > 4096 ||
      Plan.MaxAttributeBytes == 0 || Plan.MaxAttributeBytes > 32 ||
      Plan.MaxRecursionDepth == 0 || Plan.MaxRecursionDepth > 31)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "ray payload, attribute or recursion limit is outside DXR 1.1 bounds");
  if (Plan.MaxInstances == 0 || Plan.MaxGeometries == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "ray tracing requires positive instance and geometry counts");
  return llvm::Error::success();
}

} // namespace feme::direct3d
