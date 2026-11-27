#include "feme/Direct3D/ViewportPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

#include <cmath>
#include <limits>

namespace feme::direct3d {

llvm::Error validateViewportPlan(const ViewportPlan &Viewport,
                                 const ScissorRectPlan &Scissor) {
  if (!std::isfinite(Viewport.TopLeftX) || !std::isfinite(Viewport.TopLeftY) ||
      !std::isfinite(Viewport.Width) || !std::isfinite(Viewport.Height))
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "viewport position and extent must be finite");
  if (Viewport.Width <= 0.0f || Viewport.Height <= 0.0f)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "viewport width and height must be positive");
  if (!std::isfinite(Viewport.MinDepth) || !std::isfinite(Viewport.MaxDepth) ||
      Viewport.MinDepth < 0.0f || Viewport.MaxDepth > 1.0f ||
      Viewport.MinDepth >= Viewport.MaxDepth)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "viewport depth range must be ordered and within [0,1]");
  if (Scissor.Right < Scissor.Left || Scissor.Bottom < Scissor.Top)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "scissor rectangle has reversed bounds");
  return llvm::Error::success();
}

} // namespace feme::direct3d
