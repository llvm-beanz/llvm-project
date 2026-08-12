//===- ResourceLowering.cpp - CPU target resource canonicalization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceLowering.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses ResourceLoweringPass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. Canonical resource call
  // creation lands in roadmap milestone 3.
  return PreservedAnalyses::all();
}
