#ifndef FEME_DIRECT3D_IMAGECOPYPLAN_H
#define FEME_DIRECT3D_IMAGECOPYPLAN_H

#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>

namespace feme::direct3d {

struct ImageSubresourcePlan {
  uint32_t MipLevel = 0;
  uint32_t FirstArraySlice = 0;
  uint32_t ArraySliceCount = 1;
};

struct ImageCopyRegionPlan {
  ImageSubresourcePlan Source;
  ImageSubresourcePlan Destination;
  std::array<int64_t, 3> SourceOffset = {0, 0, 0};
  std::array<int64_t, 3> DestinationOffset = {0, 0, 0};
  std::array<uint64_t, 3> Extent = {1, 1, 1};
};

llvm::Error validateImageCopyRegionPlan(const ImageCopyRegionPlan &Plan);

} // namespace feme::direct3d

#endif
