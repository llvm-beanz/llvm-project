//===- PipelinePlan.h - D3D normalized pipeline plan ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_PIPELINEPLAN_H
#define FEME_DIRECT3D_PIPELINEPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme::direct3d {

enum class PrimitiveTopology : uint32_t {
  Undefined = 0,
  PointList = 1,
  LineList = 2,
  LineStrip = 3,
  TriangleList = 4,
  TriangleStrip = 5,
  PatchList = 6,
};

enum class FillMode : uint32_t {
  Wireframe = 0,
  Solid = 1,
};

enum class CullMode : uint32_t {
  None = 0,
  Front = 1,
  Back = 2,
};

struct RasterizerPlan {
  FillMode Fill = FillMode::Solid;
  CullMode Cull = CullMode::Back;
  bool FrontCounterClockwise = false;
  int32_t DepthBias = 0;
  float SlopeScaledDepthBias = 0.0f;
};

struct DepthStencilPlan {
  bool DepthEnable = true;
  bool DepthWriteEnable = true;
  bool StencilEnable = false;
};

struct RenderTargetPlan {
  uint32_t NumRenderTargets = 1;
  bool IndependentBlendEnable = false;
};

struct PipelinePlan {
  PrimitiveTopology Primitive = PrimitiveTopology::TriangleList;
  RasterizerPlan Rasterizer;
  DepthStencilPlan DepthStencil;
  RenderTargetPlan RenderTargets;
};

llvm::Error validatePipelinePlan(const PipelinePlan &Plan);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_PIPELINEPLAN_H
