#ifndef FEME_DIRECT3D_MESHDISPATCHPLAN_H
#define FEME_DIRECT3D_MESHDISPATCHPLAN_H

#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>

namespace feme::direct3d {

struct MeshDispatchPlan {
  std::array<uint32_t, 3> GroupCount = {1, 1, 1};
  uint32_t PayloadBytes = 0;
  uint32_t OutputVertices = 0;
  uint32_t OutputPrimitives = 0;
  uint64_t IndirectArgumentOffset = UINT64_MAX;
  uint64_t IndirectArgumentSize = 0;
};

llvm::Error validateMeshDispatchPlan(const MeshDispatchPlan &Plan);

} // namespace feme::direct3d

#endif
