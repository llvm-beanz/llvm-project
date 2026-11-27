#ifndef FEME_DIRECT3D_DRAWPLAN_H
#define FEME_DIRECT3D_DRAWPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct DrawPlan {
  uint32_t VertexCount = 0;
  uint32_t StartVertex = 0;
  uint32_t InstanceCount = 1;
  uint32_t StartInstance = 0;
  bool Indexed = false;
  uint32_t IndexCount = 0;
  uint32_t StartIndex = 0;
  int32_t BaseVertex = 0;
  uint64_t IndirectArgumentOffset = UINT64_MAX;
  uint64_t IndirectArgumentSize = 0;
  bool Predication = false;
};

llvm::Error validateDrawPlan(const DrawPlan &Plan);

} // namespace feme::direct3d

#endif
