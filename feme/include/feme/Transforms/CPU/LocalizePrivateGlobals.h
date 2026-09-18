//===- LocalizePrivateGlobals.h - Localize per-invocation globals -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::LocalizePrivateGlobalsPass (roadmap H170),
// which converts a SPIR-V `Private`-storage-class module-scope global
// variable (e.g. a GLSL file-scope `highp float intermediateStore;`,
// imported as a plain, non-addrspace-tagged LLVM `GlobalVariable`) that is
// used by exactly one function into a real local `alloca` at that
// function's entry, replacing every use.
//
// Why this matters: such a global is semantically *per-invocation*
// storage -- each of a wave's several invocations needs its own
// independent copy, exactly like a genuine local variable -- but, unlike
// a local variable's own `alloca`, it is never eligible for LLVM's
// ordinary SROA/mem2reg promotion (those passes only ever operate on
// `AllocaInst`, never `GlobalVariable`), and this pipeline's own
// optimizer pass only runs *after* `feme::cpu::SIMDizePass` has already
// widened the module (see `feme::OptimizerPipeline::run`'s own call site
// in CompiledStage.cpp) -- too late to help. Left as a bare
// `GlobalVariable`, `SIMDizePass` has no widening rule for it at all (only
// for an `AllocaInst`, via `FunctionWidener::collectMaskedAllocas`'s own
// `MaskedAllocas` set): every one of a wave's lanes ends up writing
// (`extractelement` + `store`, one per lane) through the very same shared
// scalar address, so only the last lane's write survives, and every
// lane's later read of it (a `llvm.masked.gather` off `WaveSize` identical
// pointer copies) sees that one surviving value instead of its own.
//
// Converting the global into a genuine per-function `alloca` here, before
// `feme::cpu::LinearizePass`/`feme::cpu::SIMDizePass` run, lets the
// now-alloca flow through `SIMDizePass`'s existing, already-correct
// masked-alloca widening machinery (`FunctionWidener::widenMaskedAlloca`/
// `widenMaskedAllocaStore`/`widenMaskedAllocaLoad`) instead, giving it
// real, independent per-lane storage. Found reducing `dEQP-VK.glsl.
// derivate.dfdx.private_store.*`'s own full-black-framebuffer failure to
// this exact shape (see feme/docs/Roadmap.md's H170 row).
//
// Deliberately narrow in scope: a `FixedVectorType` array/struct leaf is
// converted only if it agrees on its tightly packed vs. real, ABI-padded
// size (roadmap L116(a)/C8b broadened this from "scalar/fixed-vector
// value type only" to also cover a struct/array whose every leaf is one
// of those shapes -- see `isSupportedAggregateLeafType`'s own comment in
// the `.cpp` file for the full reasoning); a non-power-of-2-width vector
// leaf (e.g. `<3 x float>`) is still excluded, so as not to interact with
// `feme::cpu::LocalNarrowVectorArrayInitPass`'s own already-scoped
// tight-offset fixup for exactly that shape. An aggregate-typed global is
// also still left alone if its one using function can ever `discard`/
// `demote` (see `mayDiscardOrDemote`'s own comment for the real CTS
// regression that shape caused, and why it is excluded rather than fixed
// here) -- this exclusion does not apply to the scalar/fixed-vector case,
// which predates this row and has no such gap.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_LOCALIZEPRIVATEGLOBALS_H
#define FEME_TRANSFORMS_CPU_LOCALIZEPRIVATEGLOBALS_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Converts a `Private`-storage, address-space-0, non-constant
/// `GlobalVariable` used only by one function's own instructions into a
/// real local `alloca` in that function's entry block -- a scalar or
/// fixed-vector value type outright, or (roadmap L116(a)/C8b) a
/// struct/array whose every leaf is one of those shapes, free of a
/// non-power-of-2-width vector's ABI-padding gap, and used only by a
/// function that can never `discard`/`demote`. See the file comment
/// above for why this specific category of global needs this fixup
/// before `feme::cpu::SIMDizePass` runs.
class LocalizePrivateGlobalsPass
    : public llvm::OptionalPassInfoMixin<LocalizePrivateGlobalsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-cpu-localize-private-globals";
  }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_LOCALIZEPRIVATEGLOBALS_H
