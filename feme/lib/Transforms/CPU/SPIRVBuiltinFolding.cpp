//===- SPIRVBuiltinFolding.cpp - Fold SPIR-V builtin extractelements -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVBuiltinFolding.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

PreservedAnalyses SPIRVBuiltinFoldingPass::run(Module &M,
                                               ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
      auto *EE = dyn_cast<ExtractElementInst>(&I);
      if (!EE || !isa<ConstantInt>(EE->getIndexOperand()))
        continue;

      unsigned EltNo = cast<ConstantInt>(EE->getIndexOperand())
                          ->getZExtValue();
      Value *Folded = findScalarElement(EE->getVectorOperand(), EltNo);
      if (!Folded || Folded == EE)
        continue;

      EE->replaceAllUsesWith(Folded);
      EE->eraseFromParent();
      Changed = true;
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
