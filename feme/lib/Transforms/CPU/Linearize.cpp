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
// Roadmap R27 splits the single scalar `i1` mask both transforms thread
// through a function into the **live mask**/**side-effect mask** pair
// "Shared middle-end phases" in feme/docs/FeMeGraphicsDesign.md describes:
// `feme.stage.discard` clears both going forward, `feme.stage.demote`
// clears only the side-effect mask (keeping the invocation live for
// derivatives while suppressing further writes), and `feme.stage.is_helper`
// reads back `live && !side-effect`. See `MaskPair` and `applyStageMasks`
// below. Every ordinary masked memory access still uses the live mask (a
// `load`, and any resource-load call); every side-effecting one (a `store`,
// `atomicrmw`, or resource-store call) now uses the side-effect mask
// instead -- the two coincide exactly (`Live == SideEffect` at every point)
// for a function with no `feme.stage.discard`/`.demote` call at all, so this
// is a strict extension of milestone 6/7's behavior, not a change to it.
// Scoped, like the rest of this milestone's masking, to the same divergent-
// diamond/divergent-loop-exit shapes `DiamondFlattener`/`LoopLinearizer`
// already support -- a `feme.stage.discard`/`.demote`/`.is_helper` call
// inside a loop with no divergent exit of its own (an "otherwise uniform"
// loop) is not yet lowered by this milestone and is diagnosed rather than
// left for `feme::cpu::SIMDizePass` to mis-widen; `feme.stage.output.store`
// masking (a genuine side effect once a vertex/fragment wrapper exists to
// consume it) is left to roadmap R28, which is what builds that wrapper.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/Linearize.h"

#include "StageMaskCalls.h"
#include "feme/Analysis/CPU/WaveUniformity.h"
#include "feme/Core/ShaderStage.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
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

/// The pair of masks "Shared middle-end phases" in
/// feme/docs/FeMeGraphicsDesign.md describes: which invocations still
/// execute and contribute values (`Live`), and which are additionally
/// allowed to perform a side effect (`SideEffect`). The two are the exact
/// same value for a function with no `feme.stage.discard`/`.demote` call --
/// see `applyStageMasks` -- so every existing compute shader (which has
/// neither) sees identical behavior to before this pair existed.
struct MaskPair {
  Value *Live;
  Value *SideEffect;
};

/// Rewrites every memory access and every `feme.stage.discard`/`.demote`/
/// `.is_helper` call in \p BB, threading \p Masks through in program order
/// (mutating it in place, since a `feme.stage.discard`/`.demote` call
/// narrows what governs everything after it in the same block): a
/// `feme.cpu.resource.*` load's existing mask operand becomes
/// \p Masks.Live, a store's becomes \p Masks.SideEffect (see "Canonical
/// resource calls are similarly rewritten to masked forms" in "Phase 3",
/// and "Shared middle-end phases" in feme/docs/FeMeGraphicsDesign.md:
/// "ordinary arithmetic ... consume[s] the live mask ... every lowered
/// side effect consumes the side-effect mask"), and a plain, non-atomic,
/// non-volatile `load`/`store` is replaced with the corresponding
/// `feme.cpu.masked.load`/`.store` call carrying the same choice of mask
/// (see "Side-effecting operations ... are rewritten into the masked
/// intrinsic forms" and "Loads from addresses that could be lane-varying
/// get the same treatment", also in "Phase 3"; Phase 4 decides, from the
/// address's own uniformity, whether that becomes a broadcast scalar
/// access, a scalarized active-lane loop, or a real vector `llvm.masked.*`
/// op -- this pass always emits the masked form and lets Phase 4 pick). A
/// masked load's passthru value is zero, matching "Phase 5"'s "FeMe
/// chooses zero for deterministic reference execution" for any other lane
/// read this design leaves undefined. An `atomicrmw` (a side effect) gets
/// the same treatment against \p Masks.SideEffect, via
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
/// masking would ever matter.
///
/// `feme.stage.discard(cond)` narrows both masks by `!cond` going forward
/// (killing the invocation); `feme.stage.demote(cond)` narrows only
/// `Masks.SideEffect` (demoting to a helper invocation, still live for
/// derivatives); `feme.stage.is_helper()` is replaced with
/// `Masks.Live && !Masks.SideEffect` -- true exactly for an invocation that
/// is live but has had its side-effect mask cleared without also clearing
/// its live mask, which is precisely what `.demote` (and nothing else)
/// does. All three calls are erased once lowered.
///
/// Shared between `DiamondFlattener` (a divergent arm's masks) and
/// `LoopLinearizer` (a loop iteration's "active" masks) below. A given
/// memory access is left unmasked exactly when the mask that would govern
/// it is still the all-active constant at that point in \p BB -- checked
/// per access rather than once for the whole block, since a
/// `feme.stage.discard`/`.demote` call earlier in the same block can turn
/// an initially-constant mask into a real value partway through it.
void applyStageMasks(BasicBlock &BB, MaskPair &Masks) {
  for (Instruction &I : make_early_inc_range(BB)) {
    if (auto *Call = dyn_cast<CallInst>(&I)) {
      feme::StageOpKind Kind;
      if (feme::isStageOpCall(*Call, &Kind)) {
        IRBuilder<> B(Call);
        switch (Kind) {
        case feme::StageOpKind::Discard: {
          Value *NotCond = B.CreateNot(Call->getArgOperand(0), "discard.not");
          Masks.Live = B.CreateAnd(Masks.Live, NotCond, "live.discard");
          Masks.SideEffect =
              B.CreateAnd(Masks.SideEffect, NotCond, "sideeffect.discard");
          Call->eraseFromParent();
          continue;
        }
        case feme::StageOpKind::Demote: {
          Value *NotCond = B.CreateNot(Call->getArgOperand(0), "demote.not");
          Masks.SideEffect =
              B.CreateAnd(Masks.SideEffect, NotCond, "sideeffect.demote");
          Call->eraseFromParent();
          continue;
        }
        case feme::StageOpKind::IsHelper: {
          Value *NotSideEffect =
              B.CreateNot(Masks.SideEffect, "not.sideeffect");
          Value *Helper = B.CreateAnd(Masks.Live, NotSideEffect, "is.helper");
          Call->replaceAllUsesWith(Helper);
          Call->eraseFromParent();
          continue;
        }
        case feme::StageOpKind::OutputStore:
          createMaskedOutputStore(
              B, cast<ConstantInt>(Call->getArgOperand(0))->getZExtValue(),
              Call->getArgOperand(1), Call->getArgOperand(2),
              Call->getArgOperand(3), Call->getArgOperand(4), Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        case feme::StageOpKind::StreamEmit:
          createMaskedStreamEmit(B, Call->getArgOperand(0), Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        case feme::StageOpKind::StreamCut:
          createMaskedStreamCut(B, Call->getArgOperand(0), Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        case feme::StageOpKind::TaskPayloadStore:
          // (Roadmap H6c-a-b) `offset` (operand 0) stays a plain constant,
          // mirroring `OutputStore`'s own `ElementID` above; only `value`
          // (operand 1) is a genuine per-lane value.
          createMaskedTaskPayloadStore(B, Call->getArgOperand(0),
                                      Call->getArgOperand(1),
                                      Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        case feme::StageOpKind::SetMeshOutputs:
          // (Roadmap H6c-a-a-i) Unlike `TaskPayloadStore`, both operands
          // (`vertex_count`/`primitive_count`) are genuine per-lane values
          // here -- the SPIR-V spec guarantees they are identical across
          // every invocation that reaches this call, but nothing upstream
          // of this pass has verified that, so both are still masked and
          // widened like any other stage-op operand.
          createMaskedSetMeshOutputs(B, Call->getArgOperand(0),
                                     Call->getArgOperand(1), Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        default:
          break; // Not a mask-affecting stage op; fall through below.
        }
      }
      if (std::optional<MatchedResourceCall> Matched =
              matchResourceCall(*Call)) {
        Value *Mask = isLoad(Matched->Kind) ? Masks.Live : Masks.SideEffect;
        if (!isa<Constant>(Mask))
          Call->setArgOperand(Call->arg_size() - 1, Mask);
      }
      continue;
    }
    if (auto *RI = dyn_cast<ReturnInst>(&I)) {
      if (feme::getShaderStage(*RI->getFunction()) ==
          feme::ShaderStage::Fragment) {
        IRBuilder<> B(RI);
        createReturnMasks(B, Masks.Live, Masks.SideEffect);
      }
      continue;
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->isSimple() || isa<Constant>(Masks.Live))
        continue; // Atomic/volatile: not this milestone's problem yet.
      IRBuilder<> B(LI);
      Value *Passthru = Constant::getNullValue(LI->getType());
      CallInst *Masked =
          createMaskedLoad(B, LI->getPointerOperand(), LI->getAlign().value(),
                           Masks.Live, Passthru, LI->getName());
      // A null result means `LI`'s type is a shape `MaskIntrinsics.cpp`'s
      // `appendScalarMangling` cannot yet mangle (a matrix/aggregate
      // element type, most notably), which has already reported an error
      // through `LI`'s own `LLVMContext` (caught by `feme::cpu::
      // runPipeline`'s `ErrorDiagnosticGuard`). Leave `LI` itself
      // unmasked and unmodified rather than RAUW/erase with a call that
      // was never created, so this loop keeps making progress on the rest
      // of `BB` instead of crashing on a null `CallInst *`.
      if (!Masked)
        continue;
      LI->replaceAllUsesWith(Masked);
      LI->eraseFromParent();
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (!SI->isSimple() || isa<Constant>(Masks.SideEffect))
        continue;
      IRBuilder<> B(SI);
      CallInst *Masked =
          createMaskedStore(B, SI->getValueOperand(), SI->getPointerOperand(),
                            SI->getAlign().value(), Masks.SideEffect);
      if (!Masked) // See the load case above.
        continue;
      SI->eraseFromParent();
      continue;
    }
    if (auto *RMW = dyn_cast<AtomicRMWInst>(&I)) {
      if (isa<Constant>(Masks.SideEffect))
        continue;
      IRBuilder<> B(RMW);
      CallInst *Masked = createMaskedAtomicRMW(
          B, RMW->getOperation(), RMW->getPointerOperand(),
          RMW->getValOperand(), RMW->getAlign().value(), Masks.SideEffect,
          RMW->getName());
      if (!Masked) // See the load case above.
        continue;
      RMW->replaceAllUsesWith(Masked);
      RMW->eraseFromParent();
      continue;
    }
  }
}

/// Whether \p F calls any of the mask-affecting `feme.stage.*`
/// operations `applyStageMasks` lowers (`discard`/`demote`/`is_helper`/
/// `output.store`/roadmap R34's `stream.emit`/`stream.cut`/roadmap
/// H6c-a-b's `task.payload.store`/roadmap H6c-a-a-i's
/// `set_mesh_outputs`) -- unlike a divergent branch, these can
/// appear in an otherwise fully uniform,
/// straight-line function (e.g. an unconditional `feme.stage.discard`),
/// which still needs `DiamondFlattener` to walk it and lower them rather
/// than being left untouched as "nothing to do".
bool hasStageMaskOps(Function &F) {
  for (Instruction &I : instructions(F)) {
    auto *Call = dyn_cast<CallInst>(&I);
    feme::StageOpKind Kind;
    if (Call && feme::isStageOpCall(*Call, &Kind) &&
        (Kind == feme::StageOpKind::Discard ||
         Kind == feme::StageOpKind::Demote ||
         Kind == feme::StageOpKind::IsHelper ||
         Kind == feme::StageOpKind::OutputStore ||
         Kind == feme::StageOpKind::StreamEmit ||
         Kind == feme::StageOpKind::StreamCut ||
         Kind == feme::StageOpKind::TaskPayloadStore ||
         Kind == feme::StageOpKind::SetMeshOutputs))
      return true;
  }
  return false;
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

  /// Mutates the region \p validate already approved, threading \p Masks
  /// (the live/side-effect mask pair describing whether -- and how -- the
  /// invocation reaching \p Cur is active; see `MaskPair`) down through it,
  /// narrowing it in place as `feme.stage.discard`/`.demote` calls are
  /// encountered (see `applyStageMasks`). The edge that would otherwise
  /// land on \p End is redirected to \p RedirectTo instead (equal to \p End
  /// when no redirect is needed, e.g. for an outermost or false-side call).
  /// Returns the mask pair as narrowed by the time control reaches \p End
  /// (or returns, for the outermost call), which the caller must fold back
  /// into whatever mask it threads onward -- see the divergent-branch case
  /// in the definition for why a `select` on the branch condition, not
  /// simply reusing the pre-branch masks, is what that folding needs.
  MaskPair flatten(BasicBlock *Cur, BasicBlock *End, MaskPair Masks,
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

MaskPair DiamondFlattener::flatten(BasicBlock *Cur, BasicBlock *End,
                                   MaskPair Masks, BasicBlock *RedirectTo) {
  for (;;) {
    if (Cur == End) {
      // A trivial (already-empty) walk: nothing to redirect, `Cur` itself
      // is the join point. Only reachable when this call's own arm has no
      // blocks of its own, which `validate`'s "no empty arm" check already
      // rules out for the arms this pass creates recursive calls for; kept
      // as a defensive early return rather than an assertion.
      return Masks;
    }

    Instruction *Term = Cur->getTerminator();

    // Mirrors `validate`'s own cycle-control-edge boundary (see its
    // comment): this walk must stop here too, matching whatever `validate`
    // already approved -- leaving the loop's own iteration decision, and
    // this block's own memory ops (`LoopLinearizer` masks those with the
    // loop's own "active" masks instead), to `LoopLinearizer`/a later run
    // instead of misreading it as an ordinary diamond (whose immediate
    // post-dominator is not simply "the reconvergence point of a two-arm
    // branch"). A plain nested `if`/`else` entirely inside a loop body is
    // not this boundary, and falls through to the ordinary flattening
    // below like any other diamond.
    if (auto *Br = dyn_cast<CondBrInst>(Term);
        Br && isInCycle(Cur) &&
        (isLoopControlEdge(Cur, Br->getSuccessor(0)) ||
         isLoopControlEdge(Cur, Br->getSuccessor(1))))
      return Masks;

    applyStageMasks(*Cur, Masks);

    if (isa<ReturnInst>(Term))
      return Masks; // Only reachable at the outermost call (End == nullptr).

    if (auto *UBr = dyn_cast<UncondBrInst>(Term)) {
      BasicBlock *Succ = UBr->getSuccessor(0);
      if (Succ == End) {
        if (RedirectTo != End)
          UBr->setSuccessor(0, RedirectTo);
        return Masks;
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
      // still reconverging at the same `R`. Uniform control flow cannot
      // itself narrow the live/side-effect masks (only a
      // `feme.stage.discard`/`.demote` call can), but either arm may still
      // contain one, and unlike the divergent case below there is no
      // single physical path through both arms to select between -- only
      // one of them actually runs, via the real (preserved) branch -- so
      // the exiting masks are merged with real `phi`s at `R` instead of a
      // `select`, the same way any other value the two arms disagree on
      // would be. `TPred`/`FPred` are identified by dominance before either
      // arm is mutated, mirroring the divergent case's own classification
      // below.
      auto PredIt = pred_begin(R);
      BasicBlock *Pred0 = *PredIt++;
      BasicBlock *Pred1 = *PredIt;
      BasicBlock *TPred = DT.dominates(T, Pred0) ? Pred0 : Pred1;
      BasicBlock *FPred = TPred == Pred0 ? Pred1 : Pred0;

      MaskPair TExit = flatten(T, R, Masks, R);
      MaskPair FExit = flatten(Fsucc, R, Masks, R);

      IRBuilder<> MergeBuilder(&*R->getFirstInsertionPt());
      PHINode *LivePN =
          MergeBuilder.CreatePHI(Masks.Live->getType(), 2, "live.merge");
      LivePN->addIncoming(TExit.Live, TPred);
      LivePN->addIncoming(FExit.Live, FPred);
      PHINode *SideEffectPN = MergeBuilder.CreatePHI(
          Masks.SideEffect->getType(), 2, "sideeffect.merge");
      SideEffectPN->addIncoming(TExit.SideEffect, TPred);
      SideEffectPN->addIncoming(FExit.SideEffect, FPred);
      Masks.Live = LivePN;
      Masks.SideEffect = SideEffectPN;
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
    MaskPair TMasks{
        CondBuilder.CreateAnd(Masks.Live, Cond, "live.t"),
        CondBuilder.CreateAnd(Masks.SideEffect, Cond, "sideeffect.t")};
    MaskPair FMasks{
        CondBuilder.CreateAnd(Masks.Live, NotCond, "live.f"),
        CondBuilder.CreateAnd(Masks.SideEffect, NotCond, "sideeffect.f")};

    UncondBrInst::Create(T, Br->getIterator());
    Br->eraseFromParent();

    // The true arm always falls through into the false arm instead of
    // reconverging directly; the false arm still reconverges at `R`
    // normally -- this is the "unconditional fallthrough" the design
    // describes, see the file comment above.
    MaskPair TExit = flatten(T, R, TMasks, /*RedirectTo=*/Fsucc);
    MaskPair FExit = flatten(Fsucc, R, FMasks, /*RedirectTo=*/R);

    // Fold the two arms' (possibly `feme.stage.discard`/`.demote`-narrowed)
    // exit masks back together by the same condition that split them:
    // `select(Cond, Masks.Live & Cond, Masks.Live & !Cond)` is exactly
    // `Masks.Live` when neither arm narrowed anything, so this is a strict
    // generalization of simply reusing the pre-branch masks (what this
    // pass did before roadmap R27 added `feme.stage.discard`/`.demote`).
    IRBuilder<> MergeBuilder(&*R->getFirstInsertionPt());
    Masks.Live =
        MergeBuilder.CreateSelect(Cond, TExit.Live, FExit.Live, "live.merge");
    Masks.SideEffect = MergeBuilder.CreateSelect(
        Cond, TExit.SideEffect, FExit.SideEffect, "sideeffect.merge");

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
  // mask constant and an otherwise no-op mutation walk in that case --
  // unless the function calls a mask-affecting `feme.stage.*` operation
  // (see `hasStageMaskOps`), which needs this walk to lower it even absent
  // any divergent branch at all (e.g. an unconditional
  // `feme.stage.discard`).
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
  if (!HasDivergentBranch && !hasStageMaskOps(F) &&
      feme::getShaderStage(F) != feme::ShaderStage::Fragment)
    return false;

  MaskPair AllActive{ConstantInt::getTrue(F.getContext()),
                     ConstantInt::getTrue(F.getContext())};
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

  /// Finalizes \p Latch's backedge once its loop-carried masks are fully
  /// known (\p MasksAtLatch), returning the resulting backedge condition:
  /// \p Latch's own natural condition (if it has one -- real, uniform
  /// control flow the exit check upstream left alone), conjoined with
  /// `feme.cpu.mask.any` of \p MasksAtLatch.Live (any lane still
  /// contributing values at all; every side-effect mask is a subset of the
  /// live mask -- see `MaskPair`'s comment -- so this alone suffices), so a
  /// uniform-false natural exit still wins and a uniform-true natural
  /// continue does not resurrect a lane an earlier divergent check already
  /// deactivated. \p Latch's existing terminator is erased; the caller
  /// installs the real backedge branch using the returned condition.
  Value *closeLatch(BasicBlock *Latch, BasicBlock *Header,
                    const MaskPair &MasksAtLatch);

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

/// Roadmap H19k: `StructurizeCFG` unconditionally routes *every* two-way
/// region through a shared "Flow" reconvergence block, including a loop's
/// own uniform trip-count check whenever it sits in a block distinct from
/// the loop's latch -- the ordinary C-style `for (init; cond; ++i) { body }`
/// shape, since `cond` and `++i` land in different blocks. That splits what
/// `LoopLinearizer::linearizeCycle` needs to see as a single exit-check
/// block into two: the real check, and \p BB (a candidate "Flow" block)
/// re-deriving the identical decision from a `phi` selecting between two
/// compile-time-constant booleans, one per predecessor -- reduced from a
/// real failing `dEQP-VK.image.load_store_multisample.2d.*` case, whose
/// `for (sampleNdx...)` verification loop takes exactly this shape.
///
/// Recognizes that redundancy narrowly and, if \p BB matches it, rewrites
/// each of its predecessors to branch directly to whichever of \p BB's own
/// two successors that predecessor's own constant selects -- bypassing
/// \p BB entirely -- then erases it. Returns whether \p BB was folded away.
///
/// This is deliberately far narrower than a general jump-threading/
/// `SimplifyCFG`-style fold: it requires \p BB's condition to be a `phi`
/// located in \p BB itself with every incoming value a literal
/// `ConstantInt` (so the fold can never accidentally erase a genuine
/// divergent decision, only a compile-time-provable re-derivation of a
/// decision predecessors already made), requires exactly two predecessors
/// that resolve to \p BB's two *different* successors (so every other
/// value `phi` in \p BB has exactly one real forwarding predecessor per
/// successor once bypassed, with no ambiguity to resolve), and touches
/// nothing outside \p BB and its immediate predecessors/successors -- unlike
/// a whole-function `SimplifyCFG` pass, it cannot touch an unrelated
/// multi-exit loop's own legitimate exit-unification blocks (e.g. an early
/// `return` inside a loop body reconverging with the loop's normal fall-
/// through, `loop-early-return.ll`'s shape), since those blocks' own
/// selecting `phi` is never *entirely* constant-valued the way this one is.
bool foldRedundantFlowBlock(BasicBlock *BB) {
  auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
  if (!Br)
    return false;
  auto *CondPN = dyn_cast<PHINode>(Br->getCondition());
  if (!CondPN || CondPN->getParent() != BB)
    return false;
  if (CondPN->getNumIncomingValues() != 2)
    return false;

  BasicBlock *Preds[2];
  BasicBlock *Targets[2];
  for (unsigned I = 0; I < 2; ++I) {
    auto *K = dyn_cast<ConstantInt>(CondPN->getIncomingValue(I));
    if (!K)
      return false; // Not every incoming value is a compile-time constant.
    Preds[I] = CondPN->getIncomingBlock(I);
    Targets[I] = K->isOne() ? Br->getSuccessor(0) : Br->getSuccessor(1);
  }
  if (Preds[0] == Preds[1] || Targets[0] == Targets[1])
    return false; // Not a real two-way split once bypassed.

  // Every other `phi` in `BB` (the loop-carried values `Cond`'s own
  // decision was computed alongside) must have exactly these same two
  // predecessors too, so each can be forwarded to the matching real
  // predecessor below without ambiguity.
  for (PHINode &PN : BB->phis())
    if (&PN != CondPN && (PN.getNumIncomingValues() != 2 ||
                          PN.getBasicBlockIndex(Preds[0]) < 0 ||
                          PN.getBasicBlockIndex(Preds[1]) < 0))
      return false;

  for (unsigned I = 0; I < 2; ++I) {
    BasicBlock *Pred = Preds[I];
    BasicBlock *Target = Targets[I];
    Instruction *PredTerm = Pred->getTerminator();
    for (unsigned S = 0, SE = PredTerm->getNumSuccessors(); S != SE; ++S)
      if (PredTerm->getSuccessor(S) == BB)
        PredTerm->setSuccessor(S, Target);

    // `Target` itself may be a pure single-predecessor, phi-less relay --
    // `BreakCriticalEdges`'s own trampoline for the edge `BB` used to have
    // into it, since `BB` (a `CondBrInst`) branching into a block with more
    // than one predecessor is exactly a critical edge. Any phi actually
    // consuming one of `BB`'s own values sits at the real merge point past
    // any such chain of relays, keyed on whichever block in the chain is
    // its own immediate, still-standing predecessor -- `BB` itself only if
    // there is no relay at all.
    BasicBlock *IncomingBlock = BB;
    BasicBlock *Merge = Target;
    while (Merge->phis().empty()) {
      auto *UBr = dyn_cast<UncondBrInst>(Merge->getTerminator());
      if (!UBr)
        break;
      IncomingBlock = Merge;
      Merge = UBr->getSuccessor(0);
    }
    for (PHINode &MergePN : Merge->phis()) {
      int Idx = MergePN.getBasicBlockIndex(IncomingBlock);
      if (Idx < 0)
        continue; // `Merge`'s own natural value, unrelated to `BB`.
      Value *Forwarded = MergePN.getIncomingValue(Idx);
      if (auto *ForwardedPN = dyn_cast<PHINode>(Forwarded);
          ForwardedPN && ForwardedPN->getParent() == BB)
        Forwarded = ForwardedPN->getIncomingValueForBlock(Pred);
      MergePN.setIncomingValue(Idx, Forwarded);
      if (IncomingBlock == BB)
        MergePN.setIncomingBlock(Idx, Pred);
      // Else `IncomingBlock` is a relay that still stands, unaffected by
      // `BB`'s own removal below -- only the value needed fixing.
    }
  }

  BB->eraseFromParent();
  return true;
}

/// Repeatedly applies `foldRedundantFlowBlock` to every block \p C contains
/// besides \p Header/\p Latch until none match, folding away as many
/// redundant "Flow" re-derivations as this cycle happens to have (ordinarily
/// at most one, for the single loop-exit-check shape roadmap H19k targets).
/// Returns whether anything was folded.
bool foldRedundantFlowBlocksInCycle(CycleInfo &CI, CycleRef C,
                                    BasicBlock *Header, BasicBlock *Latch) {
  bool Changed = false;
  bool FoldedThisPass = true;
  while (FoldedThisPass) {
    FoldedThisPass = false;
    for (BasicBlock &BB : *Header->getParent()) {
      if (!CI.contains(C, &BB) || &BB == Header || &BB == Latch)
        continue;
      if (foldRedundantFlowBlock(&BB)) {
        Changed = FoldedThisPass = true;
        break; // `BB` (and the range) is invalidated; restart the scan.
      }
    }
  }
  return Changed;
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
                                  const MaskPair &MasksAtLatch) {
  auto *NaturalBr = dyn_cast<CondBrInst>(Latch->getTerminator());
  IRBuilder<> B(Latch->getTerminator());
  Value *AnyActive = createMaskAny(B, MasksAtLatch.Live, "loop.any.active");
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

  // Roadmap H19k: fold away any redundant "Flow" re-derivation of a
  // decision a predecessor already made at compile time, before looking
  // for this cycle's own single exit-check block below -- see
  // `foldRedundantFlowBlock`'s comment. Leaves a genuine divergent check
  // (whose condition is a real runtime value, not a constant-selected
  // `phi`) completely untouched.
  foldRedundantFlowBlocksInCycle(CI, C, Header, Latch);

  LLVMContext &Ctx = F.getContext();
  Type *I1Ty = Type::getInt1Ty(Ctx);
  // Two loop-carried phis (see `MaskPair`) instead of one: `feme.stage.
  // discard`/`.demote` inside this loop's body narrows one or both of them
  // per iteration, exactly as `DiamondFlattener::applyStageMasks` does for
  // a divergent diamond.
  auto makeActivePNPair = [&] {
    PHINode *LivePN =
        PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active.live");
    LivePN->insertBefore(Header->getFirstNonPHIIt());
    PHINode *SideEffectPN =
        PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active.sideeffect");
    SideEffectPN->insertBefore(Header->getFirstNonPHIIt());
    for (BasicBlock *Pred : predecessors(Header)) {
      if (CI.contains(C, Pred))
        continue;
      LivePN->addIncoming(ConstantInt::getTrue(Ctx), Pred);
      SideEffectPN->addIncoming(ConstantInt::getTrue(Ctx), Pred);
    }
    return MaskPair{LivePN, SideEffectPN};
  };
  auto addLatchIncoming = [&](MaskPair &Masks, const MaskPair &AtLatch) {
    cast<PHINode>(Masks.Live)->addIncoming(AtLatch.Live, Latch);
    cast<PHINode>(Masks.SideEffect)->addIncoming(AtLatch.SideEffect, Latch);
  };
  // Conjoins \p Masks with \p Staying (a uniform "this lane wants to keep
  // looping" condition, not a `feme.stage.discard`/`.demote` narrowing), the
  // same way for both fields -- unlike `applyStageMasks`, this never
  // affects the two masks asymmetrically.
  auto stayInLoop = [](IRBuilder<> &B, const MaskPair &Masks, Value *Staying,
                       StringRef Name) {
    return MaskPair{
        B.CreateAnd(Masks.Live, Staying, (Name + ".live").str()),
        B.CreateAnd(Masks.SideEffect, Staying, (Name + ".sideeffect").str())};
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

    MaskPair Masks = makeActivePNPair();
    applyStageMasks(*Header, Masks);
    IRBuilder<> B(HeaderExit->Br);
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(HeaderExit->Cond)
                                            : HeaderExit->Cond;
    MaskPair MasksNext = stayInLoop(B, Masks, Staying, "active.next");
    Value *Continue = createMaskAny(B, MasksNext.Live, "loop.continue");
    CondBrInst::Create(Continue, HeaderExit->StayInLoop, ExitBlock,
                       HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
    addLatchIncoming(Masks, MasksNext);
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
      // Roadmap H19k: `foldRedundantFlowBlock` above may have left
      // `CheckBlock`'s own "exit" arm reaching `ExitBlock` only through a
      // `BreakCriticalEdges` relay (a pure, single-predecessor trampoline
      // with no `phi`s of its own, left behind by breaking the critical
      // edge a redundant "Flow" block's own exit arm used to be) rather
      // than directly -- try each of `CheckBlock`'s two successors as a
      // candidate "exit" arm reaching `ExitBlock` via such a straight
      // chain before giving up. The relay itself is left as dead code:
      // `CheckExit->Br` below is always replaced with an unconditional
      // branch to `CheckExit->StayInLoop` (a real divergent exit
      // deactivates lanes rather than truly branching away, see below),
      // so nothing ever reaches it at runtime once linearized.
      auto *Br = cast<CondBrInst>(CheckBlock->getTerminator());
      for (unsigned I = 0; I != 2; ++I) {
        BasicBlock *Candidate = Br->getSuccessor(I);
        if (!straightChain(Candidate, ExitBlock))
          continue;
        ExitCheck EC;
        EC.Br = Br;
        EC.Cond = Br->getCondition();
        EC.ExitOnTrue = (I == 0);
        EC.StayInLoop = Br->getSuccessor(1 - I);
        if (CheckExit) {
          CheckExit = std::nullopt; // Both arms reach it: ambiguous.
          break;
        }
        CheckExit = EC;
      }
    }
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

    MaskPair Masks = makeActivePNPair();
    for (BasicBlock *BB : *PreChain)
      applyStageMasks(*BB, Masks);
    applyStageMasks(*CheckBlock, Masks);

    IRBuilder<> CheckBuilder(CheckExit->Br);
    Value *Staying = CheckExit->ExitOnTrue
                         ? CheckBuilder.CreateNot(CheckExit->Cond)
                         : CheckExit->Cond;
    MaskPair MasksAfterCheck =
        stayInLoop(CheckBuilder, Masks, Staying, "active.check");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see "Loops with a
    // divergent exit" below).
    UncondBrInst::Create(CheckExit->StayInLoop, CheckExit->Br->getIterator());
    CheckExit->Br->eraseFromParent();

    for (BasicBlock *BB : *PostChain)
      applyStageMasks(*BB, MasksAfterCheck);
    applyStageMasks(*Latch, MasksAfterCheck);

    Value *Continue = closeLatch(Latch, Header, MasksAfterCheck);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
    addLatchIncoming(Masks, MasksAfterCheck);
    return true;
  }

  if (!HeaderDivergent && !LatchDivergent)
    return false; // No divergence: a real uniform loop, left alone.

  MaskPair Masks = makeActivePNPair();
  applyStageMasks(*Header, Masks);
  MaskPair MasksAtLatch = Masks;
  if (HeaderDivergent) {
    IRBuilder<> B(HeaderExit->Br);
    Value *Cond = HeaderExit->Cond;
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    MasksAtLatch = stayInLoop(B, Masks, Staying, "active.header");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see the file
    // comment above).
    UncondBrInst::Create(HeaderExit->StayInLoop, HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
  }

  applyStageMasks(*Latch, MasksAtLatch);
  MaskPair MasksAfterLatchCheck = MasksAtLatch;
  Value *Continue;
  if (LatchDivergent) {
    IRBuilder<> B(LatchExit->Br);
    Value *Cond = LatchExit->Cond;
    Value *Staying = LatchExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    MasksAfterLatchCheck = stayInLoop(B, MasksAtLatch, Staying, "active.latch");
    Continue = createMaskAny(B, MasksAfterLatchCheck.Live, "loop.continue");
    CondBrInst::Create(Continue, Header, ExitBlock,
                       LatchExit->Br->getIterator());
    LatchExit->Br->eraseFromParent();
  } else {
    // The latch's own condition (if any) is real/uniform control flow and
    // stays exactly as it branches today, conjoined with "any lane still
    // active" so a uniform-false natural exit still wins, and so a
    // uniform-true natural continue does not resurrect a lane the header's
    // divergent check already deactivated.
    Continue = closeLatch(Latch, Header, MasksAtLatch);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
  }

  addLatchIncoming(Masks, MasksAfterLatchCheck);
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

    // `DiamondFlattener`/`LoopLinearizer` only lower a `feme.stage.discard`/
    // `.demote`/`.is_helper` call inside the divergent-diamond and
    // divergent-loop-exit shapes they already support (see their own
    // comments); one inside an otherwise-uniform loop is a shape neither
    // handles yet (roadmap R27 deviation) and must be diagnosed here rather
    // than left for `feme::cpu::SIMDizePass` to silently mis-widen as an
    // ordinary opaque call.
    if (hasStageMaskOps(F))
      diagnose(F, "calls a mask-affecting feme.stage.* operation "
                  "('discard'/'demote'/'is_helper') in a shape this "
                  "milestone does not lower (e.g. inside an otherwise "
                  "uniform loop)");
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
