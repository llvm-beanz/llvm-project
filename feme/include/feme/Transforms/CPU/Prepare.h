//===- Prepare.h - CPU target Phase 1: preparation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::PreparePass, "Phase 1: Preparation" in
// feme/docs/FeMeCPUDesign.md: getting a raised module into the shape every
// later CPU pipeline phase assumes (structurized control flow, no `switch`,
// `mem2reg`/SROA-promoted allocas, and a single selected/canonicalized
// compute entry point).
//
// This is currently scaffolding (roadmap milestone 1): the pass is
// registered under its final name so `feme-opt -passes=feme-cpu-prepare`
// and the rest of the CPU pipeline's command-line surface exist end to end,
// but it does not yet transform anything -- see the Roadmap / Milestones
// section of feme/docs/FeMeCPUDesign.md for when each piece of Phase 1
// lands.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_PREPARE_H
#define FEME_TRANSFORMS_CPU_PREPARE_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Phase 1: prepares a raised module for the rest of the CPU pipeline. See
/// the file comment above for current scope.
class PreparePass : public llvm::PassInfoMixin<PreparePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-prepare"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_PREPARE_H
