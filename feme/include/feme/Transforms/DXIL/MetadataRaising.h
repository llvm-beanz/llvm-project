//===- MetadataRaising.h - Raise dx.* module metadata -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::dxil::MetadataRaisingPass, the module-level
// counterpart to feme::dxil::OpRaisingPass: where that pass raises DXIL's
// `dx.op.*` calls back to `llvm.dx.*` intrinsics, this one raises DXIL's
// `dx.shaderModel`/`dx.entryPoints` named metadata back into the module
// target triple and `hlsl.*` function attributes that modern LLVM's DirectX
// backend consumes (see `llvm/lib/Analysis/DXILMetadataAnalysis.cpp`).
//
// This is the inverse of `DXILTranslateMetadata`
// (llvm/lib/Target/DirectX/DXILTranslateMetadata.cpp). Without it, an
// imported DXIL module carries a frozen `dxil-ms-dx` triple and no entry
// point information at all in the form the backend expects, so re-targeting
// it -- to DXIL or to anything else -- has no way to know which function is
// the entry point, what pipeline stage it implements, or what its thread
// group dimensions are.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_METADATARAISING_H
#define FEME_TRANSFORMS_DXIL_METADATARAISING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace dxil {

/// Raises DXIL's `dx.shaderModel`/`dx.entryPoints` named metadata into a
/// modern shader-model target triple plus `hlsl.shader`/`hlsl.numthreads`
/// function attributes, and drops the `dx.*` named metadata the DirectX
/// backend regenerates for itself. Modules with no `dx.shaderModel` metadata
/// (i.e. not DXIL-originated) are left untouched.
class MetadataRaisingPass : public llvm::PassInfoMixin<MetadataRaisingPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-dxil-raise-metadata"; }
};

} // namespace dxil
} // namespace feme

#endif // FEME_TRANSFORMS_DXIL_METADATARAISING_H
