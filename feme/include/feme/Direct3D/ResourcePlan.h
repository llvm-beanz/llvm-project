//===- ResourcePlan.h - D3D resource/subresource plan ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_RESOURCEPLAN_H
#define FEME_DIRECT3D_RESOURCEPLAN_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <optional>

namespace feme::direct3d {

enum class ResourceDimension : uint32_t {
  Buffer = 0,
  Texture1D = 1,
  Texture2D = 2,
  Texture3D = 3,
};

struct ResourcePlan {
  ResourceDimension Dimension = ResourceDimension::Buffer;
  uint64_t Width = 0;
  uint32_t Height = 1;
  uint32_t Depth = 1;
  uint32_t ArraySize = 1;
  uint32_t MipLevels = 1;
  uint32_t SampleCount = 1;
};

llvm::Error validateResourcePlan(const ResourcePlan &Plan);

std::optional<uint64_t> resolveSubresourceIndex(const ResourcePlan &Plan,
                                                uint32_t MipLevel,
                                                uint32_t ArrayIndex,
                                                uint32_t DepthSlice = 0);

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_RESOURCEPLAN_H
