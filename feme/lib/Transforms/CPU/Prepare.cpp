//===- Prepare.cpp - CPU target Phase 1: preparation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Prepare.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses PreparePass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. Structurization, `switch`
  // lowering, `mem2reg`/SROA and entry-point canonicalization land in a
  // later roadmap milestone.
  return PreservedAnalyses::all();
}
