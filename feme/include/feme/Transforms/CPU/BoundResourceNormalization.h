//===- BoundResourceNormalization.h - Bound-resource-to-heap emulation --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::BoundResourceNormalizationPass, which
// rewrites a raised shader's traditional, register-bound resource access
// into the same bindless descriptor-heap form the rest of the CPU target
// pipeline understands -- see "Bound-resource normalization" in the
// "Resource Model" section of feme/docs/FeMeCPUDesign.md. It runs
// immediately before feme::cpu::ResourceLoweringPass, which never gains a
// bound-resource case of its own: by the time it runs, every resource
// access goes through `llvm.dx.resource.handlefromheap`.
//
// Scope (roadmap milestone 11):
//
//  - Only `llvm.dx.resource.handlefrombinding` calls whose handle is one of
//    the two resource kinds `feme::cpu::ResourceLoweringPass` itself
//    canonicalizes -- `dx.TypedBuffer`/`dx.RawBuffer` -- are normalized,
//    matching that pass's own narrowing (see its header comment). A call
//    with any other handle kind is left untouched.
//  - `llvm.dx.resource.handlefromimplicitbinding` and SPIR-V's
//    `llvm.spv.resource.handlefrombinding`/`...handlefromimplicitbinding`
//    are not normalized: no in-tree raiser currently produces the DXIL
//    implicit-binding form, and SPIR-V has no raised bindless-heap
//    counterpart (`llvm.spv.resource.handlefromheap`) to rewrite into at
//    all yet (see "Resource Model"'s SPIR-V bullet). Both remain rejected
//    by `feme::cpu::checkSupportedRaisedOps`, unchanged from before this
//    pass existed.
//  - An unbounded range (`handlefrombinding`'s range-size operand is 0) or
//    two bindings that disagree about a shared (space, register) identity's
//    family or range size are left as `handlefrombinding` calls rather than
//    normalized, so `feme::cpu::checkSupportedRaisedOps` still rejects them
//    -- see that function's diagnostic for the guidance given in each case.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_BOUNDRESOURCENORMALIZATION_H
#define FEME_TRANSFORMS_CPU_BOUNDRESOURCENORMALIZATION_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Normalizes traditional, register-bound resource access into the bindless
/// descriptor-heap form the rest of the CPU pipeline understands. See the
/// file comment above for current scope.
class BoundResourceNormalizationPass
    : public llvm::PassInfoMixin<BoundResourceNormalizationPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-normalize-bound-resources"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_BOUNDRESOURCENORMALIZATION_H
