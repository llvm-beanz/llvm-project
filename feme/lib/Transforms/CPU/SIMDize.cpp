//===- SIMDize.cpp - CPU target Phase 4: widening ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SIMDize.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses SIMDizePass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. Widening rules land in
  // roadmap milestones 4 and 7.
  return PreservedAnalyses::all();
}
