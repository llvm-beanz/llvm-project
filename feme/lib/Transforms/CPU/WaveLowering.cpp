//===- WaveLowering.cpp - CPU target Phase 5: wave/builtin lowering ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveLowering.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses WaveLoweringPass::run(Module &, ModuleAnalysisManager &) {
  // Scaffolding only -- see the header comment. Builtin lowering lands in
  // roadmap milestone 4; wave intrinsic lowering in milestone 8.
  return PreservedAnalyses::all();
}
