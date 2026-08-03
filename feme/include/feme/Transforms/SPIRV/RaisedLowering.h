//===- RaisedLowering.h - Lower raised IR to SPIR-V conventions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::spirv::RaisedLoweringPass, the SPIR-V counterpart
// to feme::amdgpu::RaisedLoweringPass: it rewrites a shader's raised,
// format-agnostic LLVM IR (the `llvm.dx.*`-flavored output of
// feme::dxil::OpRaisingPass, see feme/docs/Design.md's "Per-Format
// Representation Strategy" section) into the `llvm.spv.*` intrinsics and
// `target("spirv.*")` handle types LLVM's in-tree SPIRV backend consumes.
//
// Unlike the AMDGPU direction, most of this is a near-1:1 renaming: both
// LLVM's DirectX and SPIRV backends model shader concepts -- thread indices,
// resource bindings, typed buffer accesses -- with parallel intrinsic
// families, because both are fed by the same HLSL frontend. The one real
// translation is the resource handle type: DXIL's
// `target("dx.TypedBuffer", <N x T>, ...)` becomes SPIR-V's
// `target("spirv.Image", T, ...)`, whose element type is the *scalar*
// component type with the vector width folded into the image format instead.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_SPIRV_RAISEDLOWERING_H
#define FEME_TRANSFORMS_SPIRV_RAISEDLOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace spirv {

/// Rewrites the raised, format-agnostic intrinsic calls this pass covers into
/// the SPIR-V target intrinsic calls they correspond to, so the result is
/// valid input to a SPIRV `llvm::TargetMachine`. Ops without a counterpart
/// are left unmodified, so this pass composes safely with modules that mix
/// lowered and not-yet-lowered operations.
class RaisedLoweringPass : public llvm::PassInfoMixin<RaisedLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-spirv-lower-raised"; }
};

} // namespace spirv
} // namespace feme

#endif // FEME_TRANSFORMS_SPIRV_RAISEDLOWERING_H
