//===- ResourcePlan.cpp - D3D resource/subresource plan ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/ResourcePlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

#include <algorithm>

namespace feme::direct3d {

llvm::Error validateResourcePlan(const ResourcePlan &Plan) {
  if (Plan.Width == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "resource width must be nonzero");
  if (Plan.Dimension == ResourceDimension::Buffer) {
    if (Plan.Height != 1 || Plan.Depth != 1 || Plan.ArraySize != 1 ||
        Plan.MipLevels != 1 || Plan.SampleCount != 1)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "buffer resources cannot have image array/mip/multisample state");
  } else {
    if (Plan.Height == 0 || Plan.Depth == 0 || Plan.ArraySize == 0 ||
        Plan.MipLevels == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "texture extents, arrays and mips must be nonzero");
    if (Plan.Dimension == ResourceDimension::Texture1D &&
        (Plan.Height != 1 || Plan.Depth != 1))
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "1D textures must have height/depth equal to one");
    if (Plan.Dimension != ResourceDimension::Texture3D && Plan.Depth != 1)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "non-3D texture depth must be one");
    if (Plan.Dimension == ResourceDimension::Texture3D && Plan.ArraySize != 1)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "3D textures cannot have array layers");
  }
  if (Plan.SampleCount == 0 || (Plan.SampleCount & (Plan.SampleCount - 1)) != 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "sample count must be a positive power of two");
  if (Plan.Dimension == ResourceDimension::Buffer &&
      Plan.Width > UINT64_C(0xFFFFFFFF))
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "buffer width exceeds the host checked-range limit");
  return llvm::Error::success();
}

std::optional<uint64_t> resolveSubresourceIndex(const ResourcePlan &Plan,
                                                uint32_t MipLevel,
                                                uint32_t ArrayIndex,
                                                uint32_t DepthSlice) {
  if (MipLevel >= Plan.MipLevels)
    return std::nullopt;

  uint64_t Offset = 0;
  for (uint32_t Mip = 0; Mip < MipLevel; ++Mip) {
    uint64_t MipDepth =
        Plan.Dimension == ResourceDimension::Texture3D
            ? std::max<uint64_t>(1, static_cast<uint64_t>(Plan.Depth) >> Mip)
            : 1;
    uint64_t MipLayers = Plan.Dimension == ResourceDimension::Texture3D
                             ? MipDepth
                             : Plan.ArraySize;
    if (Offset > UINT64_MAX - MipLayers)
      return std::nullopt;
    Offset += MipLayers;
  }

  if (Plan.Dimension == ResourceDimension::Texture3D) {
    if (ArrayIndex != 0)
      return std::nullopt;
    uint64_t Depth =
        std::max<uint64_t>(1, static_cast<uint64_t>(Plan.Depth) >> MipLevel);
    if (DepthSlice >= Depth)
      return std::nullopt;
    return Offset + DepthSlice;
  }
  if (ArrayIndex >= Plan.ArraySize)
    return std::nullopt;
  return Offset + ArrayIndex;
}

} // namespace feme::direct3d
