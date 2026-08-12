//===- OptimizerPipeline.cpp - FeMe IR optimization pipeline -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Optimizer/OptimizerPipeline.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/TargetMachine.h"

using namespace feme;

void OptimizerPipeline::run(llvm::Module &M, const OptimizerOptions &Opts,
                            llvm::TargetMachine *TM) const {
  llvm::PassBuilder PB(TM);

  // Registers the standard set of analyses (target-specific ones too, when
  // TM is non-null) that the default pipeline's passes expect to be able to
  // request, exactly as `opt`'s new pass manager driver does.
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // `buildPerModuleDefaultPipeline` itself dispatches to the minimal
  // `buildO0DefaultPipeline` when Level is O0, so no special-casing is
  // needed here for the "disable optimizations" (`-O0`/`-Od`) case.
  llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(Opts.Level);
  MPM.run(M, MAM);
}
