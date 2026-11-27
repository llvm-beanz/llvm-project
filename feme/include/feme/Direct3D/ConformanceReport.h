#ifndef FEME_DIRECT3D_CONFORMANCEREPORT_H
#define FEME_DIRECT3D_CONFORMANCEREPORT_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct ConformanceReport {
  uint64_t Total = 0;
  uint64_t Passed = 0;
  uint64_t Failed = 0;
  uint64_t Unsupported = 0;
  uint64_t DeviceLost = 0;
  uint64_t InternalError = 0;
};

llvm::Error validateConformanceReport(const ConformanceReport &Report);
bool isConformanceReleaseReady(const ConformanceReport &Report);

} // namespace feme::direct3d

#endif
