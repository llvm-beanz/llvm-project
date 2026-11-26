//===- BarrierPlan.h - D3D dependency/barrier plan -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_BARRIERPLAN_H
#define FEME_DIRECT3D_BARRIERPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme::direct3d {

enum class BarrierKind : uint32_t {
  Transition = 0,
  Aliasing = 1,
  UAV = 2,
};

enum class ResourceState : uint32_t {
  Common = 0,
  Present = 1,
  Indirect = 2,
  CopySource = 3,
  CopyDestination = 4,
  ResolveSource = 5,
  ResolveDestination = 6,
  VertexBuffer = 7,
  IndexBuffer = 8,
  ConstantBuffer = 9,
  ShaderResource = 10,
  UnorderedAccess = 11,
  RenderTarget = 12,
  DepthWrite = 13,
  DepthRead = 14,
};

struct SubresourceRange {
  uint32_t BaseMip = 0;
  uint32_t NumMips = 1;
  uint32_t BaseArray = 0;
  uint32_t NumArrays = 1;
};

struct BarrierPlan {
  BarrierKind Kind = BarrierKind::Transition;
  ResourceState Before = ResourceState::Common;
  ResourceState After = ResourceState::Common;
  SubresourceRange Range;
};

llvm::Error validateBarrierPlan(const std::vector<BarrierPlan> &Barriers);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_BARRIERPLAN_H
