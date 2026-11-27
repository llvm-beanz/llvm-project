#include "feme/Direct3D/ImageCopyPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateImageCopyRegionPlan(const ImageCopyRegionPlan &Plan) {
  if (Plan.Source.ArraySliceCount == 0 || Plan.Destination.ArraySliceCount == 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "image copy subresource must contain array slices");
  for (uint64_t Size : Plan.Extent) {
    if (Size == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "image copy extent must be positive");
  }
  for (int64_t Offset : Plan.SourceOffset) {
    if (Offset < 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "source copy offset must be nonnegative");
  }
  for (int64_t Offset : Plan.DestinationOffset) {
    if (Offset < 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "destination copy offset must be nonnegative");
  }
  for (uint32_t I = 0; I < 3; ++I) {
    if (static_cast<uint64_t>(Plan.SourceOffset[I]) >
        UINT64_MAX - Plan.Extent[I])
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "source copy range overflow");
    if (static_cast<uint64_t>(Plan.DestinationOffset[I]) >
        UINT64_MAX - Plan.Extent[I])
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "destination copy range overflow");
  }
  return llvm::Error::success();
}

} // namespace feme::direct3d
