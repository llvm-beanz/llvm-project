#include "feme/Direct3D/AccelerationStructurePlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error
validateAccelerationStructurePlan(const AccelerationStructurePlan &Plan) {
  if (Plan.SizeBytes == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "acceleration structure size must be positive");
  if (Plan.OffsetBytes % 256 != 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "acceleration structure offset must be 256-byte aligned");
  if (Plan.Kind == AccelerationStructureKind::BottomLevel) {
    if (Plan.GeometryCount == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "bottom-level structure requires geometry");
  } else if (Plan.InstanceCount == 0) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "top-level structure requires instances");
  }
  if (Plan.AllowCompact && Plan.AllowUpdate)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "compaction and update modes are separate build policies");
  return llvm::Error::success();
}

} // namespace feme::direct3d
