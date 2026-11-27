#include "feme/Direct3D/EnhancedBarrierPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateEnhancedBarrierPlan(const EnhancedBarrierPlan &Plan) {
  const EnhancedBarrierRange &Range = Plan.Range;
  if (Range.MipCount == 0 || Range.ArrayCount == 0 || Range.PlaneCount == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "enhanced barrier subresource range must not be empty");
  if (Plan.Phase == BarrierPhase::Single &&
      (Plan.BeforeSync == BarrierSync::None ||
       Plan.AfterSync == BarrierSync::None))
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "single-phase barrier requires explicit sync scopes");
  if (Plan.Phase == BarrierPhase::Begin && Plan.BeforeSync == BarrierSync::None)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "begin barrier requires an explicit before sync");
  if (Plan.Phase == BarrierPhase::End && Plan.AfterSync == BarrierSync::None)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "end barrier requires an explicit after sync");
  return llvm::Error::success();
}

} // namespace feme::direct3d
