//===- SPIRVUnmergeResourceLoads.cpp - Split phi-merged resource loads ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See SPIRVUnmergeResourceLoads.h for the full rationale (roadmap
// L174/L155).
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVUnmergeResourceLoads.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Returns true if \p V is a direct call to `llvm.spv.resource.getpointer`.
bool isSPIRVResourceGetPointerCall(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee &&
         Callee->getIntrinsicID() == Intrinsic::spv_resource_getpointer;
}

/// Returns true if every instruction in \p BB strictly after \p After (and
/// before \p BB's terminator) is free of any memory-write side effect --
/// i.e. hoisting a `load` that currently reads through \p BB's incoming
/// edge back to immediately after \p After cannot observe a different
/// value than the `load` it is replacing does today.
bool isSafeToHoistLoadAfter(const Instruction *After) {
  for (const Instruction *I = After->getNextNode(); I && !I->isTerminator();
       I = I->getNextNode()) {
    if (I->mayWriteToMemory())
      return false;
  }
  return true;
}

/// If \p PN is a pointer-typed `PHINode` all of whose incoming values are
/// `llvm.spv.resource.getpointer` calls each residing in their own
/// incoming block (with nothing but side-effect-free instructions between
/// that call and the block's terminator), splits every `load` using \p PN
/// as its pointer operand back into one `load` per incoming block plus a
/// new `PHINode` of the loaded values, and returns true. Otherwise leaves
/// \p PN untouched and returns false.
bool tryUnmergeResourcePointerPHI(PHINode &PN) {
  if (!PN.getType()->isPointerTy())
    return false;

  for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
    Value *InVal = PN.getIncomingValue(I);
    BasicBlock *InBB = PN.getIncomingBlock(I);
    if (!isSPIRVResourceGetPointerCall(InVal))
      return false;
    auto *GetPtr = cast<Instruction>(InVal);
    if (GetPtr->getParent() != InBB || !isSafeToHoistLoadAfter(GetPtr))
      return false;
  }

  // Collect the `load`s using this PHI as their pointer operand before
  // mutating anything -- rewriting invalidates `PN`'s use-list iterators.
  //
  // Crucially, only a `load` in `PN`'s own parent block qualifies: `PN`'s
  // incoming-block list is only guaranteed to match the *real* CFG
  // predecessors of `PN`'s own parent block. A `load` sunk into some
  // later block (past additional control flow downstream of the merge --
  // e.g. an outer, unrelated `if` that only uses the loaded value along
  // one path) is not reachable from each incoming block directly, so
  // reusing `PN`'s incoming-block list for a new `PHINode` placed at that
  // sunk `load`'s site would produce a `PHINode` whose incoming blocks no
  // longer match its own parent's actual predecessors -- invalid IR that
  // silently mis-renders rather than failing loudly (found via CTS's
  // `binding_model.shader_access.*vertex_fragment*`, roadmap L175: the
  // combined-vertex+fragment shape adds exactly this kind of extra outer
  // diamond around the switch-merged resource load).
  SmallVector<LoadInst *, 4> Loads;
  for (User *U : PN.users())
    if (auto *LI = dyn_cast<LoadInst>(U); LI &&
                                          LI->getPointerOperand() == &PN &&
                                          LI->getParent() == PN.getParent())
      Loads.push_back(LI);
  if (Loads.empty())
    return false;

  bool Changed = false;
  for (LoadInst *LI : Loads) {
    PHINode *ValuePHI = PHINode::Create(LI->getType(), PN.getNumIncomingValues(),
                                         LI->getName() + ".unmerged");
    ValuePHI->insertBefore(LI->getIterator());
    for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
      Value *InVal = PN.getIncomingValue(I);
      BasicBlock *InBB = PN.getIncomingBlock(I);
      auto *ClonedLoad = cast<LoadInst>(LI->clone());
      ClonedLoad->setOperand(LoadInst::getPointerOperandIndex(), InVal);
      ClonedLoad->setName(LI->getName() + ".unmerged.in");
      ClonedLoad->insertAfter(cast<Instruction>(InVal));
      ValuePHI->addIncoming(ClonedLoad, InBB);
    }
    LI->replaceAllUsesWith(ValuePHI);
    LI->eraseFromParent();
    Changed = true;
  }

  if (PN.use_empty())
    PN.eraseFromParent();

  return Changed;
}

} // namespace

PreservedAnalyses SPIRVUnmergeResourceLoadsPass::run(Module &M,
                                                      ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    // Collect first: `tryUnmergeResourcePointerPHI` erases the `PHINode`
    // it successfully rewrites, and may insert new ones, so iterating
    // `F`'s instructions directly while rewriting is unsafe.
    SmallVector<PHINode *, 8> Candidates;
    for (Instruction &I : instructions(F))
      if (auto *PN = dyn_cast<PHINode>(&I); PN && PN->getType()->isPointerTy())
        Candidates.push_back(PN);

    for (PHINode *PN : Candidates)
      Changed |= tryUnmergeResourcePointerPHI(*PN);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
