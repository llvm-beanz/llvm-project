//===- Tessellation.h - Tessellation state attributes -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_TESSELLATION_H
#define FEME_GRAPHICS_TESSELLATION_H

#include "feme/Graphics/PatchPipeline.h"
#include "llvm/ADT/StringRef.h"

#include <optional>

namespace llvm {
class Function;
}

namespace feme::graphics {

llvm::StringRef getTessellationDomainAttrName();
llvm::StringRef getTessellationPartitioningAttrName();
llvm::StringRef getTessellationOutputPrimitiveAttrName();
llvm::StringRef getTessellationOutputControlPointCountAttrName();

/// Returns the tessellation state encoded on \p F by the SPIR-V import path,
/// or `std::nullopt` if \p F carries no tessellation attributes at all.
std::optional<TessellationState> getTessellationState(const llvm::Function &F);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_TESSELLATION_H
