#ifndef FEME_DIRECT3D_PRESENTATIONPLAN_H
#define FEME_DIRECT3D_PRESENTATIONPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

enum class DisplayRotation : uint32_t {
  Identity = 0,
  Rotate90 = 1,
  Rotate180 = 2,
  Rotate270 = 3,
};

struct PresentationPlan {
  uint32_t Width = 0;
  uint32_t Height = 0;
  DisplayRotation Rotation = DisplayRotation::Identity;
  bool Tearing = false;
  bool HDR = false;
  bool SharedFence = false;
  bool SharedResource = false;
};

llvm::Error validatePresentationPlan(const PresentationPlan &Plan);

} // namespace feme::direct3d

#endif
