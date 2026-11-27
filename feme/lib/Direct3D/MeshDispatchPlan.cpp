#include "feme/Direct3D/MeshDispatchPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateMeshDispatchPlan(const MeshDispatchPlan &Plan) {
  for (uint32_t Count : Plan.GroupCount) {
    if (Count == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "mesh dispatch group dimensions must be positive");
  }
  if (Plan.PayloadBytes > 16384)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "mesh payload exceeds 16 KiB");
  if (Plan.OutputVertices > 256 || Plan.OutputPrimitives > 256)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "mesh output exceeds Tier 1 limits");
  if (Plan.IndirectArgumentOffset != UINT64_MAX &&
      Plan.IndirectArgumentSize == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "mesh indirect argument range must be positive");
  return llvm::Error::success();
}

} // namespace feme::direct3d
