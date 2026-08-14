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

/// Rewrites every memory access in \p BB that needs a governing mask once
/// \p Mask is not the all-active constant: a `feme.cpu.resource.*` call's
/// existing mask operand is set to \p Mask (see "Canonical resource calls
/// are similarly rewritten to masked forms" in "Phase 3"), and a plain,
/// non-atomic, non-volatile `load`/`store` is replaced with the
/// corresponding `feme.cpu.masked.load`/`.store` call carrying \p Mask (see
/// "Side-effecting operations ... are rewritten into the masked intrinsic
/// forms" and "Loads from addresses that could be lane-varying get the same
/// treatment", also in "Phase 3"; Phase 4 decides, from the address's own
/// uniformity, whether that becomes a broadcast scalar access, a
/// scalarized active-lane loop, or a real vector `llvm.masked.*` op -- this
/// pass always emits the masked form and lets Phase 4 pick). A masked
/// load's passthru value is zero, matching "Phase 5"'s "FeMe chooses zero
/// for deterministic reference execution" for any other lane read this
/// design leaves undefined. An `atomicrmw` gets the same treatment, via
/// `feme.cpu.masked.atomicrmw` (see `feme::cpu::SIMDizePass`'s
/// `widenMaskedAtomicRMW`, which turns a masked-off lane's contribution
/// into its operation's identity element rather than skipping the
/// instruction, so it never needs real per-lane control flow) -- this
/// closes roadmap milestone 7's "Scalarization fallback does not mask
/// per-lane execution" deviation (feme/docs/FeMeCPUDesign.md's Status
/// section): before this, an atomic left for `FunctionWidener`'s generic
/// scalarization fallback ran unconditionally on every lane regardless of
/// this block's governing mask, corrupting memory on behalf of a lane that
/// should have stayed inactive. An `AtomicCmpXchgInst` needs no equivalent
/// rewrite here: its `{T, i1}` result is an aggregate, already rejected by
/// `feme::cpu::SIMDizePass::checkVectorDecompositionSupported` before
/// masking would ever matter. Shared between `DiamondFlattener` (a
/// divergent arm's mask) and `LoopLinearizer` (a loop iteration's "active"
/// mask) below. A no-op when \p Mask is the all-active constant: nothing
/// outside a divergent region needs masking.
void maskMemoryOps(BasicBlock &BB, Value *Mask) {
  if (isa<Constant>(Mask))
    return;
  for (Instruction &I : make_early_inc_range(BB)) {
    if (auto *Call = dyn_cast<CallInst>(&I)) {
      if (std::optional<MatchedResourceCall> Matched = matchResourceCall(*Call))
        Call->setArgOperand(Call->arg_size() - 1, Mask);
      continue;
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->isSimple())
        continue; // Atomic/volatile: not this milestone's problem yet.
      IRBuilder<> B(LI);
      Value *Passthru = Constant::getNullValue(LI->getType());
      CallInst *Masked =
          createMaskedLoad(B, LI->getPointerOperand(), LI->getAlign().value(),
                           Mask, Passthru, LI->getName());
      LI->replaceAllUsesWith(Masked);
      LI->eraseFromParent();
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (!SI->isSimple())
        continue;
      IRBuilder<> B(SI);
      createMaskedStore(B, SI->getValueOperand(), SI->getPointerOperand(),
                        SI->getAlign().value(), Mask);
      SI->eraseFromParent();
      continue;
    }
    if (auto *RMW = dyn_cast<AtomicRMWInst>(&I)) {
      IRBuilder<> B(RMW);
      CallInst *Masked = createMaskedAtomicRMW(
          B, RMW->getOperation(), RMW->getPointerOperand(),
          RMW->getValOperand(), RMW->getAlign().value(), Mask, RMW->getName());
      RMW->replaceAllUsesWith(Masked);
      RMW->eraseFromParent();
      continue;
    }
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
  /// \p F's entry block, or from the exit block of any cycle reached along
  /// the way (see `validate`'s comment), without crossing into a cycle
  /// itself. Returns whether \p F was changed; a validation failure is
  /// diagnosed and leaves \p F untouched (returns false).
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

  /// Whether \p Target is one of \p Cur's own cycle's two loop-control
  /// edges -- a back edge to the cycle's header, or its one edge to the
  /// cycle's exit block -- as opposed to a branch target that stays
  /// entirely within the loop body. \p Cur must be a cycle member (see
  /// `isInCycle`). A two-way branch whose targets are *both* ordinary loop-
  /// body blocks is a plain nested `if`/`else` this pass flattens like any
  /// other diamond, wherever it happens to sit; one whose target is either
  /// of these two edges instead decides the loop's own iteration, which is
  /// `LoopLinearizer`'s job, not this pass's (see the class comment's "code
  /// after that cycle" case, and `feme::cpu::LoopLinearizer::
  /// linearizeCycle`'s own comment for why an internal diamond feeding that
  /// decision -- as opposed to being the decision itself -- is fine for
  /// this pass to flatten first).
  bool isLoopControlEdge(BasicBlock *Cur, BasicBlock *Target) {
    CycleRef C = CI.getCycle(Cur);
    if (Target == CI.getHeader(C))
      return true;
    SmallVector<BasicBlock *, 2> Exits;
    CI.getExitBlocks(C, Exits);
    return llvm::is_contained(Exits, Target);
  }

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
  /// (non-empty) arms, recursively -- even one entirely inside a cycle, as
  /// long as neither of its targets is that cycle's own loop-control edge
  /// (see `isLoopControlEdge`). Stops (without failing) the moment such an
  /// edge is reached, recording it in `CycleBoundaryBlocks` (see `run`) so
  /// code after that cycle -- e.g. a divergent diamond branching on a value
  /// the loop computed, as in a Mandelbrot-style escape-time loop followed
  /// by a palette lookup -- still gets its own chance at validation instead
  /// of being silently left unvisited just because it happens to follow a
  /// loop.
  bool validate(BasicBlock *Start, BasicBlock *End);

  /// Mutates the region \p validate already approved, threading \p Mask
  /// (the scalar `i1` value describing whether the invocation reaching
  /// \p Cur is active) down through it. The edge that would otherwise land
  /// on \p End is redirected to \p RedirectTo instead (equal to \p End when
  /// no redirect is needed, e.g. for an outermost or false-side call).
  void flatten(BasicBlock *Cur, BasicBlock *End, Value *Mask,
               BasicBlock *RedirectTo);

  /// The blocks `validate` stopped at (see its comment) because they were
  /// cycle members, collected across every root `run` has processed so
  /// far -- each contributes its cycle's exit block(s) as further roots,
  /// since a cycle's own body is `LoopLinearizer`'s problem, but the code
  /// after it is squarely this pass's.
  SmallPtrSet<BasicBlock *, 8> CycleBoundaryBlocks;
};

bool DiamondFlattener::validate(BasicBlock *Start, BasicBlock *End) {
  BasicBlock *Cur = Start;
  while (Cur != End) {
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

    if (isInCycle(Cur) &&
        (isLoopControlEdge(Cur, T) || isLoopControlEdge(Cur, Fsucc))) {
      CycleBoundaryBlocks.insert(Cur);
      return true; // Stop here; LoopLinearizer's problem, not an error.
    }

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
    if (Cur == End) {
      // A trivial (already-empty) walk: nothing to redirect, `Cur` itself
      // is the join point. Only reachable when this call's own arm has no
      // blocks of its own, which `validate`'s "no empty arm" check already
      // rules out for the arms this pass creates recursive calls for; kept
      // as a defensive early return rather than an assertion.
      return;
    }

    Instruction *Term = Cur->getTerminator();

    // Mirrors `validate`'s own cycle-control-edge boundary (see its
    // comment): this walk must stop here too, matching whatever `validate`
    // already approved -- leaving the loop's own iteration decision, and
    // this block's own memory ops (`LoopLinearizer` masks those with the
    // loop's own "active" mask instead), to `LoopLinearizer`/a later run
    // instead of misreading it as an ordinary diamond (whose immediate
    // post-dominator is not simply "the reconvergence point of a two-arm
    // branch"). A plain nested `if`/`else` entirely inside a loop body is
    // not this boundary, and falls through to the ordinary flattening
    // below like any other diamond.
    if (auto *Br = dyn_cast<CondBrInst>(Term);
        Br && isInCycle(Cur) &&
        (isLoopControlEdge(Cur, Br->getSuccessor(0)) ||
         isLoopControlEdge(Cur, Br->getSuccessor(1))))
      return;

    maskMemoryOps(*Cur, Mask);

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
  // Two-phase, whole-function discipline (see the class comment): every
  // root this pass will flatten -- the entry block, plus the exit block of
  // any cycle `validate` stopped at along the way (see its comment) -- is
  // validated before any of them are mutated, so a validation failure
  // anywhere leaves the whole function untouched rather than partially
  // flattened. Cycle exit blocks are roots in their own right because a
  // divergent diamond can follow a loop entirely (e.g. an escape-time loop
  // followed by a palette lookup branching on whether it converged) --
  // that diamond is just as much this pass's job as one before any loop,
  // even though the loop itself is `LoopLinearizer`'s.
  SmallVector<BasicBlock *, 4> Roots{&F.getEntryBlock()};
  SmallPtrSet<BasicBlock *, 8> Considered{&F.getEntryBlock()};
  for (unsigned I = 0; I != Roots.size(); ++I) {
    CycleBoundaryBlocks.clear();
    if (!validate(Roots[I], nullptr))
      return false;
    for (BasicBlock *CycleBlock : CycleBoundaryBlocks) {
      SmallVector<BasicBlock *, 2> Exits;
      CI.getExitBlocks(CI.getCycle(CycleBlock), Exits);
      for (BasicBlock *Exit : Exits)
        if (Considered.insert(Exit).second)
          Roots.push_back(Exit);
    }
  }

  // A validation pass that found nothing to do is common (most functions
  // have no divergent branch at all); avoid manufacturing an all-active
  // mask constant and an otherwise no-op mutation walk in that case.
  bool HasDivergentBranch = false;
  for (BasicBlock &BB : F) {
    auto *Br = dyn_cast<CondBrInst>(BB.getTerminator());
    if (!Br || !UI.isDivergentTerminator(Br))
      continue;
    if (isInCycle(&BB) && (isLoopControlEdge(&BB, Br->getSuccessor(0)) ||
                           isLoopControlEdge(&BB, Br->getSuccessor(1))))
      continue; // The loop's own iteration decision, not a diamond.
    HasDivergentBranch = true;
    break;
  }
  if (!HasDivergentBranch)
    return false;

  Value *AllActive = ConstantInt::getTrue(F.getContext());
  for (BasicBlock *Root : Roots)
    flatten(Root, nullptr, AllActive, nullptr);
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

  /// Finalizes \p Latch's backedge once its loop-carried "active" mask is
  /// fully known (\p ActiveAtLatch), returning the resulting backedge
  /// condition: \p Latch's own natural condition (if it has one -- real,
  /// uniform control flow the exit check upstream left alone), conjoined
  /// with `feme.cpu.mask.any` of \p ActiveAtLatch, so a uniform-false
  /// natural exit still wins and a uniform-true natural continue does not
  /// resurrect a lane an earlier divergent check already deactivated. \p
  /// Latch's existing terminator is erased; the caller installs the real
  /// backedge branch using the returned condition.
  Value *closeLatch(BasicBlock *Latch, BasicBlock *Header,
                    Value *ActiveAtLatch);

  bool linearizeCycle(CycleRef C);
};

/// Walks the straight, unconditional chain from \p From (inclusive) to
/// \p To (exclusive) -- every block in between (but not \p From itself,
/// which may legitimately have more than one predecessor, e.g. the loop
/// header's backedge) must be entered only via this chain's previous
/// block -- returning the blocks visited in order, or `std::nullopt` if
/// the chain does not reach \p To this way. This is the "straight-line"
/// requirement `LoopLinearizer::linearizeCycle` places on whatever lies
/// between the header/latch and a loop's exit check when that check sits
/// in neither of them directly (see its comment): in particular, the
/// extra blocks `StructurizeCFG`'s general "Flow" merge-block scheme (or
/// `feme::cpu::DiamondFlattener`, flattening a divergent diamond that
/// used to feed the check) can leave behind.
std::optional<SmallVector<BasicBlock *, 4>> straightChain(BasicBlock *From,
                                                          BasicBlock *To) {
  SmallVector<BasicBlock *, 4> Chain;
  BasicBlock *Cur = From;
  while (Cur != To) {
    if (Cur != From && Cur->getUniquePredecessor() == nullptr)
      return std::nullopt;
    Chain.push_back(Cur);
    auto *UBr = dyn_cast<UncondBrInst>(Cur->getTerminator());
    if (!UBr)
      return std::nullopt;
    Cur = UBr->getSuccessor(0);
  }
  return Chain;
}

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

Value *LoopLinearizer::closeLatch(BasicBlock *Latch, BasicBlock *Header,
                                  Value *ActiveAtLatch) {
  auto *NaturalBr = dyn_cast<CondBrInst>(Latch->getTerminator());
  IRBuilder<> B(Latch->getTerminator());
  Value *AnyActive = createMaskAny(B, ActiveAtLatch, "loop.any.active");
  Value *Continue;
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
  return Continue;
}

bool LoopLinearizer::linearizeCycle(CycleRef C) {
  // This milestone linearizes the single-exit, single-latch shape
  // `feme::cpu::verifyStructured` guarantees every cycle already has (see
  // its "unique exit block" postcondition): a header and a latch, each
  // optionally ending in a divergent exit check -- or, when neither of them
  // does, a divergent exit check in exactly one other block reached from
  // the header, and reaching the latch, each via a plain unconditional
  // chain (the shape `StructurizeCFG`'s general "Flow" merge-block scheme,
  // or `feme::cpu::DiamondFlattener` flattening a divergent diamond that
  // used to feed the check, leaves behind -- see "Loops with a divergent
  // exit" below and the Status section's milestone 6 deviation note in
  // feme/docs/FeMeCPUDesign.md). Anything else -- more than one such block,
  // or one not connected by a straight chain -- is left alone and
  // diagnosed.
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

  LLVMContext &Ctx = F.getContext();
  Type *I1Ty = Type::getInt1Ty(Ctx);
  auto makeActivePN = [&] {
    PHINode *PN = PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active");
    PN->insertBefore(Header->getFirstNonPHIIt());
    for (BasicBlock *Pred : predecessors(Header))
      if (!CI.contains(C, Pred))
        PN->addIncoming(ConstantInt::getTrue(Ctx), Pred);
    return PN;
  };

  if (Header == Latch) {
    // A single-block loop body (see `infinite-loop-divergent-exit.ll`'s
    // shape): the one exit check is simultaneously the header's and the
    // latch's, so it is linearized in one step rather than two. Anything
    // else in the cycle is the compound diamond-inside-loop shape this
    // single-block case does not generalize to (see the file comment
    // above; unlike the header/latch case below, a single-block loop's
    // exit check has nowhere else to be).
    for (BasicBlock &BB : F) {
      if (!CI.contains(C, &BB) || &BB == Header)
        continue;
      if (isa<CondBrInst>(BB.getTerminator())) {
        diagnose(F, "loop at '" + Header->getName() +
                        "' has an internal branch in '" + BB.getName() +
                        "'; only a divergent exit check in the header is "
                        "supported yet for a single-block loop (roadmap "
                        "milestone 6 deviation)");
        return false;
      }
    }

    std::optional<ExitCheck> HeaderExit = matchExitCheck(*Header, ExitBlock);
    if (!HeaderExit || !UI.isDivergentTerminator(HeaderExit->Br))
      return false; // No divergence: leave this real uniform loop alone.

    PHINode *ActivePN = makeActivePN();
    maskMemoryOps(*Header, ActivePN);
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

  std::optional<ExitCheck> HeaderExit = matchExitCheck(*Header, ExitBlock);
  std::optional<ExitCheck> LatchExit = matchExitCheck(*Latch, ExitBlock);
  bool HeaderDivergent = HeaderExit && UI.isDivergentTerminator(HeaderExit->Br);
  bool LatchDivergent = LatchExit && UI.isDivergentTerminator(LatchExit->Br);

  // Every other cycle block, if any, must instead be the single "Flow
  // merge" exit-check block described above (see the file comment).
  SmallVector<BasicBlock *, 2> OtherCondBrBlocks;
  for (BasicBlock &BB : F)
    if (CI.contains(C, &BB) && &BB != Header && &BB != Latch &&
        isa<CondBrInst>(BB.getTerminator()))
      OtherCondBrBlocks.push_back(&BB);

  if (!OtherCondBrBlocks.empty()) {
    if (OtherCondBrBlocks.size() != 1 || HeaderExit || LatchExit) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" +
                      OtherCondBrBlocks.front()->getName() +
                      "'; unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    BasicBlock *CheckBlock = OtherCondBrBlocks.front();
    std::optional<ExitCheck> CheckExit = matchExitCheck(*CheckBlock, ExitBlock);
    if (!CheckExit) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + CheckBlock->getName() +
                      "' that does not reach the loop's exit block; "
                      "unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    std::optional<SmallVector<BasicBlock *, 4>> PreChain =
        straightChain(Header, CheckBlock);
    std::optional<SmallVector<BasicBlock *, 4>> PostChain =
        straightChain(CheckExit->StayInLoop, Latch);
    if (!PreChain || !PostChain) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + CheckBlock->getName() +
                      "'; only a straight-line chain to/from the exit "
                      "check is supported yet (roadmap milestone 6 "
                      "deviation)");
      return false;
    }
    if (!UI.isDivergentTerminator(CheckExit->Br))
      return false; // No divergence: leave this real uniform loop alone.

    PHINode *ActivePN = makeActivePN();
    for (BasicBlock *BB : *PreChain)
      maskMemoryOps(*BB, ActivePN);
    maskMemoryOps(*CheckBlock, ActivePN);

    IRBuilder<> CheckBuilder(CheckExit->Br);
    Value *Staying = CheckExit->ExitOnTrue
                         ? CheckBuilder.CreateNot(CheckExit->Cond)
                         : CheckExit->Cond;
    Value *ActiveAfterCheck =
        CheckBuilder.CreateAnd(ActivePN, Staying, "active.check");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see "Loops with a
    // divergent exit" below).
    UncondBrInst::Create(CheckExit->StayInLoop, CheckExit->Br->getIterator());
    CheckExit->Br->eraseFromParent();

    for (BasicBlock *BB : *PostChain)
      maskMemoryOps(*BB, ActiveAfterCheck);
    maskMemoryOps(*Latch, ActiveAfterCheck);

    Value *Continue = closeLatch(Latch, Header, ActiveAfterCheck);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
    ActivePN->addIncoming(ActiveAfterCheck, Latch);
    return true;
  }

  if (!HeaderDivergent && !LatchDivergent)
    return false; // No divergence: a real uniform loop, left alone.

  PHINode *ActivePN = makeActivePN();
  Value *ActiveAtLatch = ActivePN;
  maskMemoryOps(*Header, ActivePN);
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

  maskMemoryOps(*Latch, ActiveAtLatch);
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
    Continue = closeLatch(Latch, Header, ActiveAtLatch);
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
