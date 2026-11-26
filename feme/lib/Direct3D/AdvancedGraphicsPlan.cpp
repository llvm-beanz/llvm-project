#include "feme/Direct3D/AdvancedGraphicsPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateAdvancedGraphicsPlan(const AdvancedGraphicsPlan &Plan) {
  if (Plan.Mesh.MaxPayloadBytes > 16384 || Plan.Mesh.MaxOutputVertices > 256 ||
      Plan.Mesh.MaxOutputPrimitives > 256 || Plan.Mesh.MaxGroupSize > 128)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "mesh limits exceed the selected Feature Level 12_2 tier");
  if (Plan.Vrs != VrsTier::None && Plan.Vrs != VrsTier::Tier1 &&
      Plan.Vrs != VrsTier::Tier2)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "unsupported variable-rate shading tier");
  return llvm::Error::success();
}

} // namespace feme::direct3d
