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
// This is currently scaffolding (roadmap milestone 1): the pass is
// registered under its final name (`feme-cpu-linearize`) so the CPU
// pipeline's command-line surface exists end to end, but it does not yet
// transform anything -- see the Roadmap / Milestones section of
// feme/docs/FeMeCPUDesign.md for when this lands (milestone 6).
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
