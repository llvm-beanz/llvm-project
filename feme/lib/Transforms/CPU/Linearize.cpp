//===- Linearize.cpp - CPU target Phase 3: linearization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses LinearizePass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. Mask construction over
  // diamond/loop CFGs lands in roadmap milestone 6.
  return PreservedAnalyses::all();
}
