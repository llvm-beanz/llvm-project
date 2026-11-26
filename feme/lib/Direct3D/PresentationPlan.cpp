#include "feme/Direct3D/PresentationPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validatePresentationPlan(const PresentationPlan &Plan) {
  if (Plan.Width == 0 || Plan.Height == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "presentation width and height must be nonzero");
  if (Plan.HDR && !Plan.SharedFence)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "HDR presentation requires shared-fence synchronization");
  if (Plan.Tearing && !Plan.SharedResource)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "tearing presentation requires shared-resource interoperability");
  return llvm::Error::success();
}

} // namespace feme::direct3d
