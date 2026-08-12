//===- EntryWrapper.cpp - CPU target Phase 6: group execution ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/EntryWrapper.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses EntryWrapperPass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. The wave loop and ABI entry
  // point land in roadmap milestone 4; barriers/groupshared in milestone 9.
  return PreservedAnalyses::all();
}
