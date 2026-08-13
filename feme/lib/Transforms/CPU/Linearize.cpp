//===- Linearize.cpp - CPU target Phase 3: linearization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 6, first half: divergent diamonds (see the header
// comment for the overall two-shape scope -- loops with a divergent exit
// are added in a following commit) and the Status section's milestone 6
// deviation note in feme/docs/FeMeCPUDesign.md for what narrowed.
//
// The transform is a read-only validation walk over the original CFG
// (using `UniformityInfo`/`DominatorTree`/`PostDominatorTree`/`CycleInfo`
// computed once, up front) that either confirms the shape this pass
// supports, or bails with a diagnostic and leaves the function completely
// untouched; followed by a mutation walk that only runs once validation for
// the whole function succeeds, so a partially-rewritten function is never
// left behind.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Reports \p Message against \p F through its diagnostic handler -- the
/// same mechanism `feme::cpu::PreparePass` uses for a phase precondition
/// that cannot be satisfied by transforming further.
void diagnose(Function &F, const Twine &Message) {
  F.getContext().emitError("feme-cpu-linearize: function '" + F.getName() +
                           "': " + Message);
}

//===----------------------------------------------------------------------===//
// Divergent diamonds.
//===----------------------------------------------------------------------===//

/// Flattens divergent two-way branches in \p F into masked, unconditional
/// data flow (see the file comment above). Loops are left untouched here --
/// `LoopLinearizer` below handles those separately -- so a straight-line
/// chain this pass walks simply stops (without error) the moment it reaches
/// a cycle's header; whatever it finds beyond that point is `LoopLinearizer`
/// or a later run's problem, not this one's.
class DiamondFlattener {
public:
  DiamondFlattener(Function &F, DominatorTree &DT, PostDominatorTree &PDT,
                   CycleInfo &CI, UniformityInfo &UI)
      : F(F), DT(DT), PDT(PDT), CI(CI), UI(UI) {}

  /// Validates and then flattens every divergent diamond reachable from
  /// \p F's entry block without crossing into a cycle. Returns whether \p F
  /// was changed; a validation failure is diagnosed and leaves \p F
  /// untouched (returns false).
  bool run();

private:
  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  CycleInfo &CI;
  UniformityInfo &UI;

  /// Whether \p BB is a cycle header/member -- the boundary this pass never
  /// crosses (see the class comment).
  bool isInCycle(BasicBlock *BB) { return CI.getCycle(BB).isValid(); }

  /// The immediate post-dominator of \p BB, or `nullptr` if none (should not
  /// happen for a divergent branch once `feme::cpu::verifyStructured` has
  /// passed, but this pass re-derives it rather than trusting that as a
  /// precondition).
  BasicBlock *immediatePostDom(BasicBlock *BB) {
    DomTreeNodeBase<BasicBlock> *Node = PDT.getNode(BB);
    if (!Node || !Node->getIDom())
      return nullptr;
    return Node->getIDom()->getBlock();
  }

  /// Read-only pass validating that the region from \p Start up to (not
  /// including) \p End -- `nullptr` for "the rest of the function" -- is one
  /// this pass can flatten: every block in it is either a straight-line
  /// unconditional chain, a `ret`, or a two-way branch (uniform or
  /// divergent) with a reconvergence point and exactly two non-trivial
  /// (non-empty) arms, recursively. Stops (without failing) the moment a
  /// cycle is reached.
  bool validate(BasicBlock *Start, BasicBlock *End);

  /// Mutates the region \p validate already approved, threading \p Mask
  /// (the scalar `i1` value describing whether the invocation reaching
  /// \p Cur is active) down through it. The edge that would otherwise land
  /// on \p End is redirected to \p RedirectTo instead (equal to \p End when
  /// no redirect is needed, e.g. for an outermost or false-side call).
  void flatten(BasicBlock *Cur, BasicBlock *End, Value *Mask,
               BasicBlock *RedirectTo);

  /// Rewrites the mask operand of every `feme.cpu.resource.*` call in
  /// \p BB to \p Mask, so a resource access under a divergent condition
  /// never touches memory on behalf of an invocation that did not take that
  /// path (see "Canonical resource calls are similarly rewritten to masked
  /// forms" in "Phase 3"). A no-op when \p Mask is the all-active constant:
  /// nothing outside a divergent region needs masking.
  void maskResourceCalls(BasicBlock &BB, Value *Mask);
};

bool DiamondFlattener::validate(BasicBlock *Start, BasicBlock *End) {
  BasicBlock *Cur = Start;
  while (Cur != End) {
    if (isInCycle(Cur))
      return true; // Stop here; LoopLinearizer's problem, not an error.

    Instruction *Term = Cur->getTerminator();
    if (isa<ReturnInst>(Term)) {
      if (End != nullptr) {
        diagnose(F, "early return under a divergent branch is not yet "
                    "supported (roadmap milestone 6 deviation)");
        return false;
      }
      return true;
    }

    if (auto *UBr = dyn_cast<UncondBrInst>(Term)) {
      Cur = UBr->getSuccessor(0);
      continue;
    }

    auto *Br = dyn_cast<CondBrInst>(Term);
    if (!Br) {
      diagnose(F, "unsupported terminator '" + Twine(Term->getOpcodeName()) +
                      "' in a linearizable region");
      return false;
    }

    BasicBlock *T = Br->getSuccessor(0);
    BasicBlock *Fsucc = Br->getSuccessor(1);
    BasicBlock *R = immediatePostDom(Cur);
    if (!R) {
      diagnose(F, "divergent branch in '" + Cur->getName() +
                      "' has no reconvergence point");
      return false;
    }
    if (T == R || Fsucc == R) {
      diagnose(F, "an empty diamond arm (in '" + Cur->getName() +
                      "') is not yet supported (roadmap milestone 6 "
                      "deviation)");
      return false;
    }
    // The reconvergence block must have exactly the two predecessors this
    // rewrite expects to redirect/select between; anything else is a merge
    // shape this milestone does not generalize to yet.
    if (!R->hasNPredecessors(2)) {
      diagnose(F, "reconvergence block '" + R->getName() +
                      "' does not have exactly two predecessors");
      return false;
    }

    if (!validate(T, R) || !validate(Fsucc, R))
      return false;
    Cur = R;
  }
  return true;
}

void DiamondFlattener::maskResourceCalls(BasicBlock &BB, Value *Mask) {
  if (isa<Constant>(Mask))
    return; // All-active: nothing to mask.
  for (Instruction &I : make_early_inc_range(BB)) {
    auto *Call = dyn_cast<CallInst>(&I);
    if (!Call)
      continue;
    std::optional<MatchedResourceCall> Matched = matchResourceCall(*Call);
    if (!Matched)
      continue;
    Call->setArgOperand(Call->arg_size() - 1, Mask);
  }
}

void DiamondFlattener::flatten(BasicBlock *Cur, BasicBlock *End, Value *Mask,
                               BasicBlock *RedirectTo) {
  for (;;) {
    maskResourceCalls(*Cur, Mask);

    if (Cur == End) {
      // A trivial (already-empty) walk: nothing to redirect, `Cur` itself
      // is the join point. Only reachable when this call's own arm has no
      // blocks of its own, which `validate`'s "no empty arm" check already
      // rules out for the arms this pass creates recursive calls for; kept
      // as a defensive early return rather than an assertion.
      return;
    }

    Instruction *Term = Cur->getTerminator();
    if (isa<ReturnInst>(Term))
      return; // Only reachable at the outermost call (End == nullptr).

    if (auto *UBr = dyn_cast<UncondBrInst>(Term)) {
      BasicBlock *Succ = UBr->getSuccessor(0);
      if (Succ == End) {
        if (RedirectTo != End)
          UBr->setSuccessor(0, RedirectTo);
        return;
      }
      Cur = Succ;
      continue;
    }

    auto *Br = cast<CondBrInst>(Term);
    BasicBlock *T = Br->getSuccessor(0);
    BasicBlock *Fsucc = Br->getSuccessor(1);
    BasicBlock *R = immediatePostDom(Cur);

    if (!UI.isDivergentTerminator(Br)) {
      // Uniform: the real branch stays; each arm is flattened on its own,
      // still reconverging at the same `R`.
      flatten(T, R, Mask, R);
      flatten(Fsucc, R, Mask, R);
      Cur = R;
      continue;
    }

    Value *Cond = Br->getCondition();

    // Every `phi` at `R` merges exactly the true-arm's and false-arm's
    // final values (`validate` established `R` has exactly two
    // predecessors); classify which is which by dominance from `T` before
    // any of this branch's edges are rewritten below.
    auto PredIt = pred_begin(R);
    BasicBlock *Pred0 = *PredIt++;
    BasicBlock *Pred1 = *PredIt;
    BasicBlock *TPred = DT.dominates(T, Pred0) ? Pred0 : Pred1;
    BasicBlock *FPred = TPred == Pred0 ? Pred1 : Pred0;

    IRBuilder<> SelBuilder(&*R->getFirstInsertionPt());
    for (PHINode &PN : make_early_inc_range(R->phis())) {
      Value *ValT = PN.getIncomingValueForBlock(TPred);
      Value *ValF = PN.getIncomingValueForBlock(FPred);
      Value *Sel = SelBuilder.CreateSelect(Cond, ValT, ValF,
                                           PN.getName() + ".linearized");
      PN.replaceAllUsesWith(Sel);
      PN.eraseFromParent();
    }

    IRBuilder<> CondBuilder(Br);
    Value *NotCond = CondBuilder.CreateNot(Cond, "not." + Cond->getName());
    Value *TMask = CondBuilder.CreateAnd(Mask, Cond, "mask.t");
    Value *FMask = CondBuilder.CreateAnd(Mask, NotCond, "mask.f");

    UncondBrInst::Create(T, Br->getIterator());
    Br->eraseFromParent();

    // The true arm always falls through into the false arm instead of
    // reconverging directly; the false arm still reconverges at `R`
    // normally -- this is the "unconditional fallthrough" the design
    // describes, see the file comment above.
    flatten(T, R, TMask, /*RedirectTo=*/Fsucc);
    flatten(Fsucc, R, FMask, /*RedirectTo=*/R);

    Cur = R;
  }
}

bool DiamondFlattener::run() {
  if (!validate(&F.getEntryBlock(), nullptr))
    return false;

  // A validation pass that found nothing to do is common (most functions
  // have no divergent branch at all); avoid manufacturing an all-active
  // mask constant and an otherwise no-op mutation walk in that case.
  bool HasDivergentBranch = false;
  for (BasicBlock &BB : F) {
    auto *Br = dyn_cast<CondBrInst>(BB.getTerminator());
    if (Br && !isInCycle(&BB) && UI.isDivergentTerminator(Br)) {
      HasDivergentBranch = true;
      break;
    }
  }
  if (!HasDivergentBranch)
    return false;

  Value *AllActive = ConstantInt::getTrue(F.getContext());
  flatten(&F.getEntryBlock(), nullptr, AllActive, nullptr);
  return true;
}

} // namespace

PreservedAnalyses LinearizePass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    DominatorTree DT(F);
    CycleInfo CI;
    CI.compute(F);
    UniformityInfo UI = computeWaveUniformity(F, DT, CI);

    PostDominatorTree PDT(F);
    Changed |= DiamondFlattener(F, DT, PDT, CI, UI).run();
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
