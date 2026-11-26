#include "feme/Direct3D/CapabilityInventory.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateCapabilityInventory(const CapabilityInventory &Inventory) {
  if (Inventory.NegotiatedDdiVersion == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "capability inventory requires a negotiated DDI version");
  if (!Inventory.Compute)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "headless compute capability is required");
  if (Inventory.FL122 && (!Inventory.Graphics || !Inventory.Mesh ||
                          !Inventory.RayTracing || !Inventory.Presentation))
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "FL 12_2 requires graphics, mesh, ray "
                                   "tracing and presentation evidence");
  return llvm::Error::success();
}

} // namespace feme::direct3d
