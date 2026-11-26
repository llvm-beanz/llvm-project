//===- PipelinePlan.cpp - D3D normalized pipeline plan --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/PipelinePlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validatePipelinePlan(const PipelinePlan &Plan) {
  if (Plan.Primitive == PrimitiveTopology::Undefined)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "pipeline primitive topology must be defined");
  if (Plan.RenderTargets.NumRenderTargets > 8)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "pipeline exceeds the eight render-target limit");
  if (Plan.RenderTargets.NumRenderTargets == 0 &&
      (Plan.DepthStencil.DepthEnable || Plan.DepthStencil.StencilEnable))
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "depth/stencil-only pipeline with no DSV is unsupported");
  if (!Plan.RenderTargets.IndependentBlendEnable) {
    for (uint32_t I = 1; I < Plan.RenderTargets.NumRenderTargets; ++I) {
      if (Plan.Blend.Attachments[I].BlendEnable ||
          Plan.Blend.Attachments[I].SrcColor !=
              Plan.Blend.Attachments[0].SrcColor ||
          Plan.Blend.Attachments[I].DstColor !=
              Plan.Blend.Attachments[0].DstColor ||
          Plan.Blend.Attachments[I].ColorOp !=
              Plan.Blend.Attachments[0].ColorOp ||
          Plan.Blend.Attachments[I].SrcAlpha !=
              Plan.Blend.Attachments[0].SrcAlpha ||
          Plan.Blend.Attachments[I].DstAlpha !=
              Plan.Blend.Attachments[0].DstAlpha ||
          Plan.Blend.Attachments[I].AlphaOp !=
              Plan.Blend.Attachments[0].AlphaOp)
        return llvm::createStringError(
            llvm::errc::invalid_argument,
            "MRT blend states differ while independent blend is disabled");
    }
  }
  return llvm::Error::success();
}

} // namespace feme::direct3d
