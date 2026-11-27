#ifndef FEME_DIRECT3D_ENHANCEDBARRIERPLAN_H
#define FEME_DIRECT3D_ENHANCEDBARRIERPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

enum class BarrierPhase : uint32_t {
  Single = 0,
  Begin = 1,
  End = 2,
};

enum class BarrierSync : uint32_t {
  None = 0,
  Compute = 1,
  Draw = 2,
  Copy = 3,
  Resolve = 4,
  Present = 5,
};

enum class BarrierAccess : uint32_t {
  NoAccess = 0,
  ShaderRead = 1,
  ShaderWrite = 2,
  CopyRead = 3,
  CopyWrite = 4,
  ResolveRead = 5,
  ResolveWrite = 6,
  RenderTarget = 7,
  DepthRead = 8,
  DepthWrite = 9,
  Present = 10,
};

struct EnhancedBarrierRange {
  uint32_t FirstMip = 0;
  uint32_t MipCount = 1;
  uint32_t FirstArray = 0;
  uint32_t ArrayCount = 1;
  uint32_t FirstPlane = 0;
  uint32_t PlaneCount = 1;
};

struct EnhancedBarrierPlan {
  BarrierPhase Phase = BarrierPhase::Single;
  BarrierSync BeforeSync = BarrierSync::None;
  BarrierSync AfterSync = BarrierSync::None;
  BarrierAccess BeforeAccess = BarrierAccess::NoAccess;
  BarrierAccess AfterAccess = BarrierAccess::NoAccess;
  EnhancedBarrierRange Range;
};

llvm::Error validateEnhancedBarrierPlan(const EnhancedBarrierPlan &Plan);

} // namespace feme::direct3d

#endif
