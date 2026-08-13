//===- Linearize.cpp - CPU target Phase 3: linearization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 6. See the header comment for the two shapes this
// implements (divergent diamonds, loops with a divergent exit) and the
// Status section's milestone 6 deviation note in
// feme/docs/FeMeCPUDesign.md for what narrowed.
//
// Both transforms share the same two-step structure: a read-only validation
// walk over the original CFG (using `UniformityInfo`/`DominatorTree`/
// `PostDominatorTree`/`CycleInfo` computed once, up front) that either
// confirms the shape this pass supports and records what it needs, or bails
// with a diagnostic and leaves the function completely untouched; and a
// mutation walk that only runs once validation for the whole function
// succeeds, so a partially-rewritten function is never left behind.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
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

/// Rewrites the mask operand of every `feme.cpu.resource.*` call in \p BB to
/// \p Mask, so a resource access under a divergent condition never touches
/// memory on behalf of an invocation that did not take that path (see
/// "Canonical resource calls are similarly rewritten to masked forms" in
/// "Phase 3"). Shared between `DiamondFlattener` (a divergent arm's mask)
/// and `LoopLinearizer` (a loop iteration's "active" mask) below. A no-op
/// when \p Mask is the all-active constant: nothing outside a divergent
/// region needs masking.
void maskResourceCalls(BasicBlock &BB, Value *Mask) {
  if (isa<Constant>(Mask))
    return;
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

//===----------------------------------------------------------------------===//
// Loops with a divergent exit.
//===----------------------------------------------------------------------===//

/// Linearizes a loop whose only divergent control is an exit check in its
/// header and/or its latch (see the file comment above): the natural
/// backedge condition, if any, is conjoined with `feme.cpu.mask.any` of a
/// loop-carried "active" mask that a divergent exit check updates instead of
/// really branching away.
class LoopLinearizer {
public:
  LoopLinearizer(Function &F, CycleInfo &CI, UniformityInfo &UI)
      : F(F), CI(CI), UI(UI) {}

  /// Validates and linearizes every leaf cycle in \p F matching the shape
  /// this pass supports. Returns whether \p F was changed.
  bool run();

private:
  Function &F;
  CycleInfo &CI;
  UniformityInfo &UI;

  /// The exit-check shape a single loop block can have: a conditional
  /// branch where exactly one successor is the loop's shared exit block and
  /// the other stays inside the loop.
  struct ExitCheck {
    CondBrInst *Br = nullptr;
    Value *Cond = nullptr;
    BasicBlock *StayInLoop = nullptr;
    bool ExitOnTrue = false;
  };

  /// Recognizes \p BB's terminator as an `ExitCheck` targeting \p ExitBlock,
  /// or `std::nullopt` if it isn't a conditional branch to/from it at all.
  std::optional<ExitCheck> matchExitCheck(BasicBlock &BB,
                                          BasicBlock *ExitBlock);

  bool linearizeCycle(CycleRef C);
};

std::optional<LoopLinearizer::ExitCheck>
LoopLinearizer::matchExitCheck(BasicBlock &BB, BasicBlock *ExitBlock) {
  auto *Br = dyn_cast<CondBrInst>(BB.getTerminator());
  if (!Br)
    return std::nullopt;
  if (Br->getSuccessor(0) != ExitBlock && Br->getSuccessor(1) != ExitBlock)
    return std::nullopt;

  ExitCheck Result;
  Result.Br = Br;
  Result.Cond = Br->getCondition();
  Result.ExitOnTrue = Br->getSuccessor(0) == ExitBlock;
  Result.StayInLoop =
      Result.ExitOnTrue ? Br->getSuccessor(1) : Br->getSuccessor(0);
  return Result;
}

bool LoopLinearizer::linearizeCycle(CycleRef C) {
  // This milestone only linearizes the single-exit, single-latch shape
  // `feme::cpu::verifyStructured` guarantees every cycle already has (see
  // its "unique exit block" postcondition): a header and a latch, each
  // optionally ending in a divergent exit check, with no other blocks in
  // between (a divergent exit check nested inside a further internal
  // diamond, as in `loop-continue.ll`'s shape, is deferred -- see the
  // Status section's milestone 6 deviation note).
  BasicBlock *Header = CI.getHeader(C);
  SmallVector<BasicBlock *, 2> ExitBlocks;
  CI.getExitBlocks(C, ExitBlocks);
  if (ExitBlocks.size() != 1)
    return false; // Not this pass's problem to diagnose; verifyStructured
                  // owns that postcondition and should already have failed.
  BasicBlock *ExitBlock = ExitBlocks.front();

  BasicBlock *Latch = nullptr;
  for (BasicBlock *Pred : predecessors(Header)) {
    if (!CI.contains(C, Pred))
      continue;
    if (Latch) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has more than one latch; unsupported (roadmap "
                      "milestone 6 deviation)");
      return false;
    }
    Latch = Pred;
  }
  if (!Latch) {
    diagnose(F, "loop at '" + Header->getName() + "' has no latch");
    return false;
  }

  // Anything besides "header, optionally the latch too, each with at most
  // one exit check straight to the shared exit block" is the compound
  // diamond-inside-loop shape this milestone defers.
  for (BasicBlock &BB : F) {
    if (!CI.contains(C, &BB) || &BB == Header || &BB == Latch)
      continue;
    if (isa<CondBrInst>(BB.getTerminator())) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + BB.getName() +
                      "'; only a divergent exit check in the header and/or "
                      "latch is supported yet (roadmap milestone 6 "
                      "deviation)");
      return false;
    }
  }

  std::optional<ExitCheck> HeaderExit = matchExitCheck(*Header, ExitBlock);
  bool HeaderDivergent = HeaderExit && UI.isDivergentTerminator(HeaderExit->Br);

  LLVMContext &Ctx = F.getContext();
  Type *I1Ty = Type::getInt1Ty(Ctx);
  PHINode *ActivePN = PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active");
  ActivePN->insertBefore(Header->getFirstNonPHIIt());
  for (BasicBlock *Pred : predecessors(Header))
    if (!CI.contains(C, Pred))
      ActivePN->addIncoming(ConstantInt::getTrue(Ctx), Pred);

  if (Header == Latch) {
    // A single-block loop body (see `infinite-loop-divergent-exit.ll`'s
    // shape): the one exit check is simultaneously the header's and the
    // latch's, so it is linearized in one step rather than two.
    if (!HeaderDivergent) {
      ActivePN->eraseFromParent(); // No divergence: leave this real uniform
                                   // loop alone; undo the speculative phi.
      return false;
    }
    maskResourceCalls(*Header, ActivePN);
    IRBuilder<> B(HeaderExit->Br);
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(HeaderExit->Cond)
                                            : HeaderExit->Cond;
    Value *ActiveNext = B.CreateAnd(ActivePN, Staying, "active.next");
    Value *Continue = createMaskAny(B, ActiveNext, "loop.continue");
    CondBrInst::Create(Continue, HeaderExit->StayInLoop, ExitBlock,
                       HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
    ActivePN->addIncoming(ActiveNext, Latch);
    return true;
  }

  std::optional<ExitCheck> LatchExit = matchExitCheck(*Latch, ExitBlock);
  bool LatchDivergent = LatchExit && UI.isDivergentTerminator(LatchExit->Br);
  if (!HeaderDivergent && !LatchDivergent) {
    ActivePN->eraseFromParent(); // No divergence: a real uniform loop, left
                                 // alone; undo the phi this function already
                                 // inserted speculatively.
    return false;
  }

  Value *ActiveAtLatch = ActivePN;
  maskResourceCalls(*Header, ActivePN);
  if (HeaderDivergent) {
    IRBuilder<> B(HeaderExit->Br);
    Value *Cond = HeaderExit->Cond;
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    ActiveAtLatch = B.CreateAnd(ActivePN, Staying, "active.header");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see the file
    // comment above).
    UncondBrInst::Create(HeaderExit->StayInLoop, HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
  }

  maskResourceCalls(*Latch, ActiveAtLatch);
  Value *ActiveAfterLatchCheck = ActiveAtLatch;
  Value *Continue;
  if (LatchDivergent) {
    IRBuilder<> B(LatchExit->Br);
    Value *Cond = LatchExit->Cond;
    Value *Staying = LatchExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    ActiveAfterLatchCheck = B.CreateAnd(ActiveAtLatch, Staying, "active.latch");
    Continue = createMaskAny(B, ActiveAfterLatchCheck, "loop.continue");
    CondBrInst::Create(Continue, Header, ExitBlock,
                       LatchExit->Br->getIterator());
    LatchExit->Br->eraseFromParent();
  } else {
    // The latch's own condition (if any) is real/uniform control flow and
    // stays exactly as it branches today, conjoined with "any lane still
    // active" so a uniform-false natural exit still wins, and so a
    // uniform-true natural continue does not resurrect a lane the header's
    // divergent check already deactivated.
    auto *NaturalBr = dyn_cast<CondBrInst>(Latch->getTerminator());
    IRBuilder<> B(Latch->getTerminator());
    Value *AnyActive = createMaskAny(B, ActiveAtLatch, "loop.any.active");
    if (NaturalBr) {
      Value *NaturalCond = NaturalBr->getCondition();
      bool ContinueOnTrue = NaturalBr->getSuccessor(0) == Header;
      Value *NaturalContinue =
          ContinueOnTrue ? NaturalCond : B.CreateNot(NaturalCond);
      Continue = B.CreateAnd(NaturalContinue, AnyActive, "loop.continue");
      NaturalBr->eraseFromParent();
    } else {
      Continue = AnyActive;
      Latch->getTerminator()->eraseFromParent();
    }
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
  }

  ActivePN->addIncoming(ActiveAfterLatchCheck, Latch);
  return true;
}

bool LoopLinearizer::run() {
  bool Changed = false;
  SmallVector<CycleRef, 8> Worklist(CI.toplevel_begin(), CI.toplevel_end());
  while (!Worklist.empty()) {
    CycleRef C = Worklist.pop_back_val();
    // Innermost cycles first: an outer loop containing this one is not
    // itself linearized by this milestone (nested loops are future work),
    // but its inner cycle might still be a supported shape on its own.
    for (CycleRef Child : CI.children(C))
      Worklist.push_back(Child);
    if (!CI.children(C).empty())
      continue; // Only leaf cycles match this milestone's supported shape.
    Changed |= linearizeCycle(C);
  }
  return Changed;
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

    {
      PostDominatorTree PDT(F);
      Changed |= DiamondFlattener(F, DT, PDT, CI, UI).run();
    }

    // The diamond flattening pass above may have changed the CFG (and thus
    // invalidated `DT`/`CI`/`UI`; `PostDominatorTree` was already
    // block-scoped above). Loops are structurally untouched by it, but
    // recompute everything fresh regardless, since it is cheap next to
    // getting this wrong.
    DominatorTree DT2(F);
    CycleInfo CI2;
    CI2.compute(F);
    UniformityInfo UI2 = computeWaveUniformity(F, DT2, CI2);
    Changed |= LoopLinearizer(F, CI2, UI2).run();
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
