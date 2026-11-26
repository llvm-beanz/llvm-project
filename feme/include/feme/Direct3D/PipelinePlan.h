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

enum class CompareOp : uint32_t {
  Never = 0,
  Less = 1,
  Equal = 2,
  LessEqual = 3,
  Greater = 4,
  NotEqual = 5,
  GreaterEqual = 6,
  Always = 7,
};

enum class BlendFactor : uint32_t {
  Zero = 0,
  One = 1,
  SrcColor = 2,
  InvSrcColor = 3,
  SrcAlpha = 4,
  InvSrcAlpha = 5,
  DstColor = 6,
  InvDstColor = 7,
  DstAlpha = 8,
  InvDstAlpha = 9,
};

enum class BlendOp : uint32_t {
  Add = 0,
  Subtract = 1,
  ReverseSubtract = 2,
  Min = 3,
  Max = 4,
};

enum class StencilOp : uint32_t {
  Keep = 0,
  Zero = 1,
  Replace = 2,
  IncrementClamp = 3,
  DecrementClamp = 4,
  Invert = 5,
};

struct BlendAttachmentPlan {
  bool BlendEnable = false;
  BlendFactor SrcColor = BlendFactor::One;
  BlendFactor DstColor = BlendFactor::Zero;
  BlendOp ColorOp = BlendOp::Add;
  BlendFactor SrcAlpha = BlendFactor::One;
  BlendFactor DstAlpha = BlendFactor::Zero;
  BlendOp AlphaOp = BlendOp::Add;
};

struct BlendPlan {
  bool AlphaToCoverage = false;
  BlendAttachmentPlan Attachments[8];
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
  CompareOp DepthCompare = CompareOp::Less;
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
  BlendPlan Blend;
};

llvm::Error validatePipelinePlan(const PipelinePlan &Plan);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_PIPELINEPLAN_H
