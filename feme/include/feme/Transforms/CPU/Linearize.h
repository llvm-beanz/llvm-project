//===- Linearize.h - CPU target Phase 3: linearization -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::LinearizePass, "Phase 3: Linearization and
// Predication" in feme/docs/FeMeCPUDesign.md: turning divergent control flow
// into data flow over an explicit `i1` execution mask, before any widening
// happens.
//
// Roadmap milestone 6 implements two of this phase's shapes, each on its own
// (`feme::cpu::computeWaveUniformity`-classified) function:
//
//  - A **divergent diamond** (a two-way branch whose condition is divergent,
//    with a reconvergence point -- see `feme::cpu::verifyStructured`'s "every
//    divergent branch reconverges" postcondition): the branch becomes
//    unconditional fallthrough into its true side, whose tail is redirected
//    into its false side instead of the reconvergence block, and any `phi`
//    at the reconvergence block becomes a `select` on the branch condition.
//    Diamonds nest (a divergent branch inside another's arm, or a uniform
//    branch inside a divergent arm) by recursing the same rewrite on each
//    arm before splicing it into the outer one.
//  - A **loop with a divergent exit** (see "Loops with divergent exits" in
//    "Phase 3"): the header gains a loop-carried "active" mask, a divergent
//    exit branch inside the loop becomes an unconditional continuation that
//    updates the mask instead of really exiting, and the natural backedge
//    condition is conjoined with `feme.cpu.mask.any` of that mask so the
//    loop keeps iterating until every lane is done.
//
// See the Status section's milestone 6 deviation note in
// feme/docs/FeMeCPUDesign.md for what narrowed relative to the full design
// (e.g. a uniform branch nested inside a divergent arm's arm, an empty
// diamond arm, and masking ordinary `load`/`store` rather than only the
// canonical `feme.cpu.resource.*` calls, are all deferred).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_LINEARIZE_H
#define FEME_TRANSFORMS_CPU_LINEARIZE_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 3: linearizes divergent control flow into masked data flow. See the
/// file comment above for current scope.
class LinearizePass : public llvm::PassInfoMixin<LinearizePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-linearize"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_LINEARIZE_H
