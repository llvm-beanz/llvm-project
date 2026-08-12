//===- VerifyStructured.cpp - CPU target Phase 1 postcondition check -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/VerifyStructured.h"

#include "feme/Analysis/CPU/WaveUniformity.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CycleInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Reports \p Message against \p F to \p ErrOS, if given.
void report(raw_ostream *ErrOS, const Function &F, const Twine &Message) {
  if (ErrOS)
    *ErrOS << "feme-cpu-verify-structured: function '" << F.getName()
          << "': " << Message << "\n";
}

/// No `switch`: the linearizer only understands two-way branches (see
/// `feme::cpu::PreparePass`, which runs `LowerSwitch` for exactly this
/// reason).
bool checkNoSwitch(Function &F, raw_ostream *ErrOS) {
  bool Ok = true;
  for (BasicBlock &BB : F) {
    if (!isa<SwitchInst>(BB.getTerminator()))
      continue;
    report(ErrOS, F, "block '" + BB.getName() + "' still has a switch");
    Ok = false;
  }
  return Ok;
}

/// No critical edges: `StructurizeCFG` is documented to remove them, and the
/// linearizer's mask-merging at a branch's targets assumes none remain.
bool checkNoCriticalEdges(Function &F, raw_ostream *ErrOS) {
  bool Ok = true;
  for (BasicBlock &BB : F) {
    Instruction *T = BB.getTerminator();
    for (unsigned I = 0, E = T->getNumSuccessors(); I != E; ++I) {
      if (!isCriticalEdge(T, I))
        continue;
      report(ErrOS, F,
             "critical edge from '" + BB.getName() + "' to '" +
                 T->getSuccessor(I)->getName() + "'");
      Ok = false;
    }
  }
  return Ok;
}

/// Every cycle, at every nesting level, is reducible (single-entry) with a
/// unique exit block: `StructurizeCFG` (via `FixIrreducible`/
/// `UnifyLoopExits`) is supposed to guarantee both, and the linearizer's
/// loop handling assumes them.
bool checkCycles(Function &F, CycleInfo &CI, raw_ostream *ErrOS) {
  bool Ok = true;
  SmallVector<CycleRef, 8> Worklist(CI.toplevel_begin(),
                                               CI.toplevel_end());
  while (!Worklist.empty()) {
    CycleRef C = Worklist.pop_back_val();
    if (!CI.isReducible(C)) {
      report(ErrOS, F,
             "irreducible cycle at '" + CI.getHeader(C)->getName() + "'");
      Ok = false;
    } else {
      SmallVector<BasicBlock *, 4> ExitBlocks;
      CI.getExitBlocks(C, ExitBlocks);
      SmallPtrSet<BasicBlock *, 4> UniqueExits(ExitBlocks.begin(),
                                               ExitBlocks.end());
      if (UniqueExits.size() > 1) {
        report(ErrOS, F,
               "cycle at '" + CI.getHeader(C)->getName() +
                   "' has more than one exit block");
        Ok = false;
      }
    }
    for (CycleRef Child : CI.children(C))
      Worklist.push_back(Child);
  }
  return Ok;
}

/// Every divergent branch has a reconvergence point: the immediate
/// post-dominator the linearizer merges masks back together at. A missing
/// one (e.g. one arm never returns) is exactly the shape the linearizer
/// cannot handle.
bool checkDivergentBranchesReconverge(Function &F, DominatorTree &DT,
                                      CycleInfo &CI,
                                      PostDominatorTree &PDT,
                                      raw_ostream *ErrOS) {
  UniformityInfo UI = computeWaveUniformity(F, DT, CI);
  bool Ok = true;
  for (BasicBlock &BB : F) {
    auto *BI = dyn_cast<CondBrInst>(BB.getTerminator());
    if (!BI || UI.isUniformTerminator(BI))
      continue;
    DomTreeNodeBase<BasicBlock> *Node = PDT.getNode(&BB);
    if (Node && Node->getIDom())
      continue;
    report(ErrOS, F,
           "divergent branch in '" + BB.getName() +
               "' has no reconvergence point");
    Ok = false;
  }
  return Ok;
}

} // namespace

bool feme::cpu::verifyStructured(Function &F, raw_ostream *ErrOS) {
  if (F.isDeclaration())
    return true;

  // Each check runs (and reports) independently rather than short-circuiting
  // on the first failure, so a single `-verify-structured` invocation
  // reports every postcondition a shape violates, not just the first one
  // found.
  bool Ok = checkNoSwitch(F, ErrOS);
  Ok = checkNoCriticalEdges(F, ErrOS) && Ok;

  DominatorTree DT(F);
  CycleInfo CI;
  CI.compute(F);
  Ok = checkCycles(F, CI, ErrOS) && Ok;

  PostDominatorTree PDT(F);
  Ok = checkDivergentBranchesReconverge(F, DT, CI, PDT, ErrOS) && Ok;

  return Ok;
}
