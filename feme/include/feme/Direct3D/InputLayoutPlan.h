#ifndef FEME_DIRECT3D_INPUTLAYOUTPLAN_H
#define FEME_DIRECT3D_INPUTLAYOUTPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::direct3d {

enum class InputSemantic : uint32_t {
  Position = 0,
  Normal = 1,
  Texcoord = 2,
  Color = 3,
  Tangent = 4,
  BlendIndices = 5,
  BlendWeight = 6,
};

enum class InputFormat : uint32_t {
  Float32 = 0,
  Float32x2 = 1,
  Float32x3 = 2,
  Float32x4 = 3,
  Uint32 = 4,
  Uint32x2 = 5,
  Uint32x3 = 6,
  Uint32x4 = 7,
};

struct InputElementPlan {
  uint32_t Slot = 0;
  uint32_t Offset = 0;
  uint32_t Stride = 0;
  uint32_t SemanticIndex = 0;
  InputSemantic Semantic = InputSemantic::Position;
  InputFormat Format = InputFormat::Float32;
};

struct InputLayoutPlan {
  std::vector<InputElementPlan> Elements;
};

llvm::Error validateInputLayoutPlan(const InputLayoutPlan &Plan);

} // namespace feme::direct3d

#endif
