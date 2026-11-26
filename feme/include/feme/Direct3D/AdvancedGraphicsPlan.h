#ifndef FEME_DIRECT3D_ADVANCEDGRAPHICSPLAN_H
#define FEME_DIRECT3D_ADVANCEDGRAPHICSPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

struct MeshPlan {
  uint32_t MaxPayloadBytes = 16384;
  uint32_t MaxGroupSize = 128;
  uint32_t MaxOutputVertices = 256;
  uint32_t MaxOutputPrimitives = 256;
};

enum class VrsTier : uint32_t { None = 0, Tier1 = 1, Tier2 = 2 };

struct AdvancedGraphicsPlan {
  MeshPlan Mesh;
  VrsTier Vrs = VrsTier::None;
  bool SamplerFeedback = false;
  bool ConservativeRaster = false;
  bool TiledResources = false;
  bool DepthBounds = false;
};

llvm::Error validateAdvancedGraphicsPlan(const AdvancedGraphicsPlan &Plan);

} // namespace feme::direct3d

#endif
