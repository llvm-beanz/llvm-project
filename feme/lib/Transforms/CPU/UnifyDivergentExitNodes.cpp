//===- UnifyDivergentExitNodes.cpp - CPU target Phase 1 early-return fix -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/UnifyDivergentExitNodes.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

bool feme::cpu::unifyDivergentExitNodes(Function &F) {
  SmallVector<BasicBlock *, 4> ReturningBlocks;
  for (BasicBlock &BB : F)
    if (isa<ReturnInst>(BB.getTerminator()))
      ReturningBlocks.push_back(&BB);

  if (ReturningBlocks.size() <= 1)
    return false;

  BasicBlock *NewRetBlock =
      BasicBlock::Create(F.getContext(), "feme.unified.return", &F);
  IRBuilder<> B(NewRetBlock);
  PHINode *RetVal = nullptr;
  if (F.getReturnType()->isVoidTy()) {
    B.CreateRetVoid();
  } else {
    RetVal = B.CreatePHI(F.getReturnType(), ReturningBlocks.size(),
                         "feme.unified.retval");
    B.CreateRet(RetVal);
  }

  for (BasicBlock *BB : ReturningBlocks) {
    auto *RI = cast<ReturnInst>(BB->getTerminator());
    if (RetVal)
      RetVal->addIncoming(RI->getReturnValue(), BB);
    RI->eraseFromParent();
    UncondBrInst::Create(NewRetBlock, BB);
  }

  return true;
}
