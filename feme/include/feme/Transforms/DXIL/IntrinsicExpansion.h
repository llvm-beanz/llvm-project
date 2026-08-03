//===- IntrinsicExpansion.h - Expand llvm.dx.* math intrinsics --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::dxil::IntrinsicExpansionPass, which expands the
// `llvm.dx.*` math intrinsics that have no equivalent outside LLVM's DirectX
// target into plain, target-agnostic LLVM IR.
//
// feme::dxil::OpRaisingPass deliberately raises DXIL's `dx.op.*` calls to
// whichever intrinsic `DXILOpLowering` lowered them *from*, which for a
// handful of HLSL-specific operations (`frac`, `saturate`, `rsqrt`, integer
// multiply-add, the dot products) is a `llvm.dx.*` intrinsic only the DirectX
// backend knows how to select. That is exactly right when re-emitting DXIL,
// but leaves any other target unable to select the result. This pass closes
// that gap once, for every target, rather than each target-specific lowering
// pass re-deriving the same identities.
//
// LLVM's own `DXILIntrinsicExpansion` pass does the same job in the forward
// direction, but is private to the DirectX target, so it cannot be reused
// here.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_INTRINSICEXPANSION_H
#define FEME_TRANSFORMS_DXIL_INTRINSICEXPANSION_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace dxil {

/// Expands the `llvm.dx.*` math intrinsics with a context-free definition in
/// terms of standard LLVM operations. Intrinsics without one -- the wave
/// operations, the resource ops, the pixel shader derivatives -- are left
/// unmodified, so this pass composes safely with targets that handle some of
/// them natively.
class IntrinsicExpansionPass
    : public llvm::PassInfoMixin<IntrinsicExpansionPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-dxil-expand-intrinsics"; }
};

} // namespace dxil
} // namespace feme

#endif // FEME_TRANSFORMS_DXIL_INTRINSICEXPANSION_H
