#include "feme/Direct3D/ConformanceReport.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateConformanceReport(const ConformanceReport &Report) {
  uint64_t Classified = 0;
  const uint64_t Values[] = {Report.Passed, Report.Failed, Report.Unsupported,
                             Report.DeviceLost, Report.InternalError};
  for (uint64_t Value : Values) {
    if (Classified > UINT64_MAX - Value)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "conformance report count overflow");
    Classified += Value;
  }
  if (Classified > Report.Total)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "conformance result classes exceed total");
  return llvm::Error::success();
}

bool isConformanceReleaseReady(const ConformanceReport &Report) {
  llvm::Error Err = validateConformanceReport(Report);
  if (Err) {
    llvm::consumeError(std::move(Err));
    return false;
  }
  return Report.Total != 0 && Report.Passed == Report.Total &&
         Report.Failed == 0 && Report.Unsupported == 0 &&
         Report.DeviceLost == 0 && Report.InternalError == 0;
}

} // namespace feme::direct3d
