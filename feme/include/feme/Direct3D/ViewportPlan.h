#ifndef FEME_DIRECT3D_VIEWPORTPLAN_H
#define FEME_DIRECT3D_VIEWPORTPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct ViewportPlan {
  float TopLeftX = 0.0f;
  float TopLeftY = 0.0f;
  float Width = 0.0f;
  float Height = 0.0f;
  float MinDepth = 0.0f;
  float MaxDepth = 1.0f;
};

struct ScissorRectPlan {
  int64_t Left = 0;
  int64_t Top = 0;
  int64_t Right = 0;
  int64_t Bottom = 0;
};

llvm::Error validateViewportPlan(const ViewportPlan &Viewport,
                                 const ScissorRectPlan &Scissor);

} // namespace feme::direct3d

#endif
