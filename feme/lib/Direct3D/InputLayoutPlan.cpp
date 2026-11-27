#include "feme/Direct3D/InputLayoutPlan.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

#include <set>
#include <tuple>

namespace feme::direct3d {

uint32_t inputFormatBytes(InputFormat Format) {
  switch (Format) {
  case InputFormat::Float32:
  case InputFormat::Uint32:
    return 4;
  case InputFormat::Float32x2:
  case InputFormat::Uint32x2:
    return 8;
  case InputFormat::Float32x3:
  case InputFormat::Uint32x3:
    return 12;
  case InputFormat::Float32x4:
  case InputFormat::Uint32x4:
    return 16;
  }
  return 0;
}

llvm::Error validateInputLayoutPlan(const InputLayoutPlan &Plan) {
  if (Plan.Elements.empty())
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "input layout must contain at least one element");
  std::set<std::tuple<uint32_t, uint32_t, InputSemantic>> SemanticBindings;
  for (const InputElementPlan &Element : Plan.Elements) {
    if (Element.Stride == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "input-slot stride must be positive");
    uint32_t FormatBytes = inputFormatBytes(Element.Format);
    if (FormatBytes == 0)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "input format is unsupported");
    if (Element.Offset > Element.Stride ||
        FormatBytes > Element.Stride - Element.Offset)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "input element exceeds its slot stride");
    auto Key =
        std::make_tuple(Element.Slot, Element.SemanticIndex, Element.Semantic);
    if (!SemanticBindings.insert(Key).second)
      return llvm::createStringError(llvm::errc::invalid_argument,
                                     "duplicate input semantic binding");
  }
  return llvm::Error::success();
}

} // namespace feme::direct3d
