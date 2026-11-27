#include "feme/Direct3D/DrawPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

llvm::Error validateDrawPlan(const DrawPlan &Plan) {
  if (Plan.InstanceCount == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "draw instance count must be positive");
  if (Plan.Indexed) {
    if (Plan.IndexCount == 0)
      return llvm::createStringError(
          llvm::errc::invalid_argument,
          "indexed draw must contain at least one index");
  } else if (Plan.VertexCount == 0) {
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "non-indexed draw must contain vertices");
  }
  if (Plan.Indexed && (static_cast<uint64_t>(Plan.StartIndex) +
                       static_cast<uint64_t>(Plan.IndexCount)) > UINT32_MAX)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "indexed draw range overflow");
  if (!Plan.Indexed && (static_cast<uint64_t>(Plan.StartVertex) +
                        static_cast<uint64_t>(Plan.VertexCount)) > UINT32_MAX)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "non-indexed draw range overflow");
  if (Plan.IndirectArgumentOffset != UINT64_MAX &&
      Plan.IndirectArgumentSize == 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "indirect argument range must be positive");
  return llvm::Error::success();
}

} // namespace feme::direct3d
