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
#include "feme/Transforms/CPU/ImageCalls.h"
#include "feme/Transforms/CPU/MaskIntrinsics.h"
#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
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
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

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
/// (Roadmap H31) `DiamondFlattener::flatten` always creates a fresh
/// `live.merge`/`sideeffect.merge` `phi` at a uniform diamond's own
/// reconvergence point, even when neither arm's own mask actually differs
/// from the other's (the overwhelmingly common case: neither arm contains
/// its own `discard`/`demote`) -- deliberately, since an *outer* diamond
/// nested around this one may thread that exact `phi` value further up to
/// its own reconvergence block by identity, something a later pass
/// (`nested-uniform-loop-in-divergent-diamond.ll`'s own regression test)
/// relies on. A `phi [X, BB1], [X, BB2]` is not itself a `Constant`, even
/// though it is trivially always `X` -- so a plain `isa<Constant>(Mask)`
/// check no longer recognizes an all-active mask as such once it has
/// passed through even one such redundant merge, let alone several in a
/// row, wrongly treating every later, still-genuinely-uniform memory
/// access as needing masking. This looks through a chain of such
/// trivially-redundant phis (every incoming value, ignoring the phi's own
/// self-referential back-edges if any, is the identical `Value`) down to
/// the real shared value they all forward, so the classification below
/// sees through them to whatever that value actually is.
static Value *lookThroughTrivialPhi(Value *V) {
  SmallPtrSet<PHINode *, 8> Seen;
  while (auto *PN = dyn_cast<PHINode>(V)) {
    if (!Seen.insert(PN).second)
      break; // A cycle: give up rather than loop forever.
    Value *Common = nullptr;
    bool AllSame = true;
    for (Value *Incoming : PN->incoming_values()) {
      if (Incoming == PN)
        continue; // Ignore the phi's own back-edge to itself.
      if (!Common)
        Common = Incoming;
      else if (Common != Incoming) {
        AllSame = false;
        break;
      }
    }
    if (!AllSame || !Common)
      break;
    V = Common;
  }
  return V;
}

/// Whether \p V is a compile-time constant, or is trivially always one
/// underneath a chain of redundant merge phis -- see
/// `lookThroughTrivialPhi`'s own comment for why that distinction matters.
static bool isKnownConstantMask(Value *V) {
  return isa<Constant>(lookThroughTrivialPhi(V));
}

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
          // (Roadmap H6c-a-b) `offset` (operand 0) is passed through
          // unchanged here regardless of whether it happens to be a plain
          // compile-time constant (the common case, mirroring
          // `OutputStore`'s own `ElementID` above) or (roadmap L47/L49) a
          // genuinely dynamic per-invocation `Value` --
          // `createMaskedTaskPayloadStore` mangles its own callee purely
          // off `Offset`'s actual type, so either shape is handled
          // generically at this phase; only `SIMDize.cpp`'s later widening
          // needs to treat the two cases differently. `value` (operand 1)
          // is always a genuine per-lane value.
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
        case feme::StageOpKind::EmitMeshTasks:
          // (Roadmap H6s) Mirrors `SetMeshOutputs` immediately above
          // exactly: all three operands (`group_count_x/y/z`) are
          // genuine per-lane values here, spec-guaranteed identical
          // across every invocation that reaches this call but not
          // verified as such by anything upstream of this pass, so all
          // three are still masked and widened like any other stage-op
          // operand.
          createMaskedEmitMeshTasks(B, Call->getArgOperand(0),
                                    Call->getArgOperand(1),
                                    Call->getArgOperand(2), Masks.SideEffect);
          Call->eraseFromParent();
          continue;
        default:
          break; // Not a mask-affecting stage op; fall through below.
        }
      }
      if (std::optional<MatchedResourceCall> Matched =
              matchResourceCall(*Call)) {
        Value *Mask = isLoad(Matched->Kind) ? Masks.Live : Masks.SideEffect;
        if (!isKnownConstantMask(Mask))
          Call->setArgOperand(Call->arg_size() - 1, Mask);
      }
      // A `feme.cpu.image.*` call's own trailing mask operand needs the
      // exact same divergent-region threading a `feme.cpu.resource.*`
      // call's already gets immediately above -- both start out as the
      // compile-time constant `true` `SPIRVResourceLoweringPass` gives
      // every such call (see `lowerImageAccesses`), relying entirely on
      // this pass to narrow it to the real per-branch/per-iteration
      // predicate. Missing this case left an image store/atomic inside a
      // divergent diamond's "true" arm running unconditionally for the
      // whole wave -- not just the lanes that actually reach that arm --
      // matching `FunctionWidener::widenImageCall`'s own `LaneMaskBase`
      // choice (a store or atomic's real side effect needs
      // `Masks.SideEffect`; a plain load only needs `Masks.Live`).
      if (std::optional<MatchedImageCall> Matched = matchImageCall(*Call)) {
        Value *Mask = (Matched->Texel || Matched->AtomicValue)
                          ? Masks.SideEffect
                          : Masks.Live;
        if (!isKnownConstantMask(Mask))
          Call->setArgOperand(Call->arg_size() - 1, Mask);
      }
      // (roadmap L85) `WaveActiveBallot`/`subgroupBallot`'s own predicate
      // operand (operand 0) must reflect exactly the invocations that are
      // both requesting a `true` bit *and* still active at this exact
      // program point: its result is a ballot over "the group's currently
      // active invocations" (the SPIR-V/GLSL spec's own `subgroupBallot`
      // wording), which is no longer implicit in which arm of a real
      // branch reached it once this pass has flattened that branch away.
      // Missing this let a `subgroupBallot(true)` sitting in a divergent
      // arm (e.g. guarded by `subgroupElect()`'s complement, as
      // `dEQP-VK.subgroups.ballot_other.compute.subgroupballotfindlsb`'s
      // own shader does) see every wave-active lane instead of just the
      // lanes that actually reached that arm, producing a stale/too-wide
      // ballot mask -- found reducing that exact CTS failure down to this
      // shape. `Env.EntryMask` (the whole function's entry mask, still
      // ANDed in later by `FunctionWidener::widenWaveCall`) already covers
      // "is this invocation part of the group at all"; this pass only
      // needs to additionally narrow by `Masks.Live` for "is it still
      // active *here*".
      if (Function *Callee = Call->getCalledFunction()) {
        Intrinsic::ID ID = Callee->getIntrinsicID();
        if ((ID == Intrinsic::dx_wave_ballot ||
             ID == Intrinsic::spv_subgroup_ballot) &&
            !isKnownConstantMask(Masks.Live)) {
          IRBuilder<> B(Call);
          Call->setArgOperand(0, B.CreateAnd(Call->getArgOperand(0),
                                             Masks.Live,
                                             "ballot.pred.masked"));
        }
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
      if (!LI->isSimple() || isKnownConstantMask(Masks.Live))
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
      if (!SI->isSimple() || isKnownConstantMask(Masks.SideEffect))
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
      if (isKnownConstantMask(Masks.SideEffect))
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
/// `set_mesh_outputs`/roadmap H6s's `emit_mesh_tasks`) -- unlike a
/// divergent branch, these can
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
         Kind == feme::StageOpKind::SetMeshOutputs ||
         Kind == feme::StageOpKind::EmitMeshTasks))
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

  /// Roadmap H95a: for every block `flatten` itself stopped at (the same
  /// boundary `CycleBoundaryBlocks` records during `validate`, see above),
  /// the `MaskPair` that was actually in effect there -- i.e. the mask
  /// describing whether the invocation reaching that block at all is live,
  /// not merely "every invocation" -- so a cycle-exit root reached only
  /// from inside some still-divergent enclosing region (e.g. a loop nested
  /// in `if (laneId == 0) { ... }`) can be seeded with that real mask
  /// instead of unconditionally assuming every lane reaches it (see `run`).
  DenseMap<BasicBlock *, MaskPair> CycleBoundaryMasks;

  /// Roadmap H95a: the reverse of the mapping `run` builds from
  /// `CycleBoundaryBlocks` to each cycle's exit block(s) -- for a given
  /// exit-block root, every boundary block whose walk reached it, so `run`
  /// can look up (via `CycleBoundaryMasks`) every mask that reaches that
  /// root and use it directly when they all agree (see `run`).
  DenseMap<BasicBlock *, SmallVector<BasicBlock *, 2>> ExitToBoundaryBlocks;
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
         isLoopControlEdge(Cur, Br->getSuccessor(1)))) {
      // Roadmap H95a: record the real mask reaching this cycle's boundary
      // here, so `run` can seed that cycle's exit-block root(s) with it
      // below instead of assuming every lane unconditionally reaches them.
      CycleBoundaryMasks[Cur] = Masks;
      return Masks;
    }

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
      Value *Sel;
      if (isa<PoisonValue>(ValF) || isa<UndefValue>(ValF))
        Sel = ValT;
      else if (isa<PoisonValue>(ValT) || isa<UndefValue>(ValT))
        Sel = ValF;
      else
        Sel = SelBuilder.CreateSelect(Cond, ValT, ValF,
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
      for (BasicBlock *Exit : Exits) {
        // Roadmap H95a: remember which boundary block(s) feed this exit
        // root, so the mutation phase below can look up the real mask(s)
        // `flatten` records for them (see `CycleBoundaryMasks`) instead of
        // always assuming every lane reaches it.
        ExitToBoundaryBlocks[Exit].push_back(CycleBlock);
        if (Considered.insert(Exit).second)
          Roots.push_back(Exit);
      }
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
  for (BasicBlock *Root : Roots) {
    // Roadmap H95a: `Root` is either `F`'s entry block (truly reached by
    // every lane) or a cycle's exit block reached only once that cycle's
    // own enclosing region -- possibly still a divergent one, e.g. a loop
    // nested inside `if (laneId == 0) { ... }` -- lets control through at
    // all. For the latter, seed `flatten` with the real mask(s) its own
    // cycle-boundary block(s) recorded (see `CycleBoundaryMasks`) rather
    // than unconditionally assuming every lane reaches it: leaving that
    // assumption in place let a trailing scalar store inside such a root
    // keep whatever placeholder mask it already had (typically a
    // hardcoded `true` from before this pass ever ran, since a uniform
    // sub-diamond doesn't narrow it any further), silently overwriting the
    // correct result the narrower mask's lanes had already computed with a
    // stale/wrong one. When a root's boundary blocks disagree on the mask
    // reaching them (only possible if a `feme.stage.discard`/`.demote`
    // inside a uniform sub-diamond narrows some paths but not others
    // before they all reach the same cycle boundary), fall back to the
    // pre-existing `AllActive` behavior rather than guessing.
    MaskPair EntryMasks = AllActive;
    auto BoundaryIt = ExitToBoundaryBlocks.find(Root);
    if (BoundaryIt != ExitToBoundaryBlocks.end()) {
      bool Seen = false;
      bool Mixed = false;
      MaskPair Candidate = AllActive;
      for (BasicBlock *BoundaryBlock : BoundaryIt->second) {
        auto MaskIt = CycleBoundaryMasks.find(BoundaryBlock);
        if (MaskIt == CycleBoundaryMasks.end())
          continue;
        if (!Seen) {
          Candidate = MaskIt->second;
          Seen = true;
        } else if (Candidate.Live != MaskIt->second.Live ||
                   Candidate.SideEffect != MaskIt->second.SideEffect) {
          Mixed = true;
          break;
        }
      }
      if (Seen && !Mixed)
        EntryMasks = Candidate;
    }
    flatten(Root, nullptr, EntryMasks, nullptr);
  }
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

  /// Roadmap L40: computed once, before any cycle in \p F is linearized
  /// (see `LinearizePass::run`), and deliberately *not* recomputed per
  /// cycle -- `foldRedundantFlowBlocksInCycle`/
  /// `peelConstantFlowPredecessorsInCycle` below mutate (and, for a fully
  /// redundant "Flow" block, delete) blocks belonging to whichever cycle
  /// is currently being linearized, which would leave `CI` (and any
  /// `UniformityInfo` built from it afterward) holding dangling
  /// `BasicBlock` pointers for any *other*, not-yet-processed cycle that
  /// still structurally contains one of those now-deleted blocks (e.g. an
  /// outer loop whose body contains the leaf cycle currently being
  /// linearized) -- recomputing a "fresh" `UniformityInfo` against the
  /// same, already-mutated `CI` here previously crashed
  /// `GenericUniformityInfo`'s own cycle traversal exactly this way on a
  /// real, nested-loop `dEQP-VK.mesh_shader.ext.misc.maximize_primitives`
  /// shader. The original, pre-mutation `UI` remains valid for every
  /// block this pass still cares about below (`Header`/`Latch` are never
  /// peeled or folded away, and `PeeledFrom` -- not `UI` -- is what
  /// disambiguates a peeled pass-through block from a genuine divergent
  /// check; see `collectUniformPassThroughRegion`'s own comment) -- so
  /// there is no need to recompute it at all.
  UniformityInfo &UI;

  /// The exit-check shape a single loop block can have: a conditional
  /// branch where exactly one successor is the loop's shared exit block and
  /// the other stays inside the loop.
  struct ExitCheck {
    CondBrInst *Br = nullptr;
    Value *Cond = nullptr;
    BasicBlock *StayInLoop = nullptr;
    bool ExitOnTrue = false;
    // The block whose own terminator (or, for a relay match, whose own
    // "into the relay chain" successor -- see `matchExitCheckWithRelay`)
    // directly reaches `ExitBlock`: `BB` itself for a direct match, or
    // the relay chain's own first hop for a relay match. Roadmap H94a:
    // this is the block whose own incoming contribution to any of
    // `ExitBlock`'s *other* phis (besides the live/side-effect masks,
    // which `addLatchIncoming` handles separately) is the semantically
    // correct value to also carry along the new `Latch`->`ExitBlock`
    // edge this milestone's "never really exit here, defer to Latch"
    // strategy installs -- see `linearizeCycle`'s own use of it.
    BasicBlock *RelayBlock = nullptr;
  };

  /// Recognizes \p BB's terminator as an `ExitCheck` targeting \p ExitBlock,
  /// or `std::nullopt` if it isn't a conditional branch to/from it at all.
  std::optional<ExitCheck> matchExitCheck(BasicBlock &BB,
                                          BasicBlock *ExitBlock);

  /// Roadmap H19k: like `matchExitCheck`, but additionally tries each of
  /// \p BB's own two successors as a candidate "exit" arm reaching \p
  /// ExitBlock only through a plain, single-predecessor straight chain
  /// (see `straightChain`) when neither successor literally *is* \p
  /// ExitBlock -- `BreakCriticalEdges`'s own relay trampoline is the
  /// common real-world case left behind once `foldRedundantFlowBlock`/
  /// `peelConstantFlowPredecessors` bypass a redundant re-derivation.
  std::optional<ExitCheck> matchExitCheckWithRelay(BasicBlock &BB,
                                                   BasicBlock *ExitBlock);

  /// Roadmap L42: generalizes the "pass-through" tolerance from a *chain*
  /// that tolerated at most one relayed pass-through block per step (an
  /// earlier, now-removed `chainToleratingUniformExits`) to a full uniform
  /// *subregion* between \p From (inclusive) and \p To (exclusive),
  /// tolerating a genuinely branching (not just chained) nested diamond of
  /// further uniform checks along the way. A real
  /// `dEQP-VK.mesh_shader.ext.misc.payload_read` shader's own
  /// verification loop nests exactly two such checks: its outer, uniform
  /// `for`-style trip-count check's own "skip the body" arm rejoins the
  /// real divergent check's own `StructurizeCFG`-built merge block
  /// directly, rather than reaching it only via a single straight,
  /// singly-entered relay chain the narrower chain-based model required --
  /// so neither of that outer check's own two arms ever reaches \p To that
  /// way, even though every block along the way remains provably uniform.
  /// A walk may also legitimately end at \p ExitBlock directly rather than
  /// \p To: `peelConstantFlowPredecessors`'s own redirect (see its
  /// comment) rewires a peeled predecessor straight to whichever of \p
  /// To's own successors its constant selects, which is \p ExitBlock
  /// itself whenever that predecessor's own peeled decision was the
  /// "exit now" arm -- bypassing \p To entirely on that path, the same
  /// way `matchExitCheckWithRelay`'s own relay tolerates it. Returns
  /// every block visited (order not significant: each is masked with the
  /// same, still-unnarrowed `MaskPair` regardless of which of this
  /// region's own arms a given lane's real, uniform control flow actually
  /// takes -- see `applyStageMasks`'s own per-block application at the
  /// call site), or `std::nullopt` if the region cannot be shown to reach
  /// \p To (or \p ExitBlock) this way: escaping \p C's own cycle, ending
  /// in anything but an `UnCondBr`/`CondBr` terminator, or a `CondBr`
  /// that is itself genuinely divergent (only \p To's own check may be
  /// that).
  std::optional<SmallPtrSet<BasicBlock *, 8>>
  collectUniformPassThroughRegion(BasicBlock *From, BasicBlock *To,
                                  BasicBlock *ExitBlock, CycleRef C,
                                  UniformityInfo &UI,
                                  const SmallPtrSetImpl<BasicBlock *> &PeeledFrom);

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
///
/// Roadmap H94a: `StructurizeCFG`'s own `loop.exit.guard` companion block
/// (see this file's own H94a comment on `collapseTrivialRelayBlocksInCycle`
/// for the full real-world shape this was reduced from) wraps an otherwise
/// exactly-matching condition `phi` in one logical negation
/// (`%Guard.inv = xor i1 %Cond, true`) before branching on it -- looks
/// through that single optional wrapper here (returning \p Negated) so
/// both this function and `peelConstantFlowPredecessors` below recognize
/// the shape either way; every incoming-value "which successor does this
/// select" test the two callers perform needs to flip accordingly whenever
/// \p Negated comes back `true`.
static PHINode *getFlowConditionPhi(CondBrInst *Br, BasicBlock *BB,
                                    bool &Negated, Instruction *&CondUser) {
  Value *Cond = Br->getCondition();
  CondUser = Br;
  Negated = false;
  if (auto *Bin = dyn_cast<Instruction>(Cond);
      Bin && Bin->getOpcode() == Instruction::Xor && Bin->getParent() == BB) {
    auto *K = dyn_cast<ConstantInt>(Bin->getOperand(1));
    if (K && K->isOne()) {
      Cond = Bin->getOperand(0);
      CondUser = Bin;
      Negated = true;
    }
  }
  auto *CondPN = dyn_cast<PHINode>(Cond);
  if (!CondPN || CondPN->getParent() != BB)
    return nullptr;
  return CondPN;
}

bool foldRedundantFlowBlock(BasicBlock *BB) {
  auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
  if (!Br)
    return false;
  bool Negated;
  Instruction *CondUser;
  PHINode *CondPN = getFlowConditionPhi(Br, BB, Negated, CondUser);
  if (!CondPN)
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
    bool ExitOnSucc0 = Negated ? !K->isOne() : K->isOne();
    Targets[I] = ExitOnSucc0 ? Br->getSuccessor(0) : Br->getSuccessor(1);
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

  // Roadmap L88: find (without mutating anything yet) each direction's own
  // `Merge`/`IncomingBlock` pair -- the same phi-less-relay walk the
  // mutating loop below performs -- so every one of `BB`'s own phis' real
  // uses can be checked *before* committing to erasing `BB`. This fold's
  // own rewiring below only ever forwards a `BB`-defined phi's value into
  // an *already-existing* phi at one of these two `Merge` blocks (one that
  // already names `IncomingBlock`, i.e. `BB` or its relay, as one of its
  // own incoming blocks) -- it was never taught to chase down any *other*
  // kind of use. `feme::cpu::DiamondFlattener` (this same file, above) can
  // leave exactly such another kind of use behind: it threads its own
  // live/side-effect mask `PHINode`s (see `MaskPair`) through the call
  // stack of its own recursive `flatten`, not through the ordinary
  // successor-phi chain this fold understands, so a mask `PHINode` it adds
  // to a loop's own reconvergence block (e.g. one `StructurizeCFG` already
  // shaped like this fold's own target, per `H19k`'s own precedent) can end
  // up consumed directly by an *outer*, unrelated `select`/`PHINode`
  // wherever `DiamondFlattener`'s own enclosing diamond happens to
  // reconverge -- nowhere near either `Merge` here. Folding `BB` away
  // regardless left that outer consumer holding a `Use` of a `Value` this
  // fold was about to destroy, a real `llvm::Value::~Value` "Uses remain
  // when a value is destroyed!" abort (reduced from a real
  // `dEQP-VK.subgroups.shuffle.*` verification shader's own nested
  // uniform-loop-inside-a-divergent-diamond shape). Bailing out here
  // (leaving `BB` -- and the harmless, if redundant, re-derivation it
  // performs -- in place) is the safe, conservative choice once such an
  // unaccounted-for use is found, mirroring this fold's own already-narrow,
  // "never guess" design elsewhere.
  BasicBlock *Merges[2];
  BasicBlock *IncomingBlocks[2];
  for (unsigned I = 0; I < 2; ++I) {
    BasicBlock *IncomingBlock = BB;
    BasicBlock *Merge = Targets[I];
    while (Merge->phis().empty()) {
      auto *UBr = dyn_cast<UncondBrInst>(Merge->getTerminator());
      if (!UBr)
        break;
      IncomingBlock = Merge;
      Merge = UBr->getSuccessor(0);
    }
    Merges[I] = Merge;
    IncomingBlocks[I] = IncomingBlock;
  }

  for (PHINode &PN : BB->phis()) {
    for (Use &U : PN.uses()) {
      auto *UserInst = cast<Instruction>(U.getUser());
      if (&PN == CondPN && UserInst == CondUser)
        continue; // `CondUser` (`Br`, or the `xor` it wraps) is erased
                   // along with `BB` itself; not a leak.
      auto *MergePN = dyn_cast<PHINode>(UserInst);
      bool Forwarded = false;
      for (unsigned I = 0; I < 2 && !Forwarded; ++I)
        Forwarded = MergePN && MergePN->getParent() == Merges[I] &&
                    MergePN->getIncomingBlock(U) == IncomingBlocks[I];
      if (!Forwarded)
        return false; // An escaping use this fold cannot safely relocate.
    }
  }

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
    // there is no relay at all. (Recomputed identically to the read-only
    // walk above -- `Merges`/`IncomingBlocks` -- rather than reused, since
    // the loop above intentionally runs before any mutation here.)
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

/// Roadmap L40: `foldRedundantFlowBlock` above requires *every* incoming
/// value of \p BB's own condition `phi` to be a literal constant before
/// touching it at all -- correctly conservative, since eliminating \p BB
/// entirely only makes sense once none of its predecessors carries a
/// genuinely divergent decision. A real `dEQP-VK.mesh_shader.ext.misc.
/// payload_read`-shaped loop's own verification `for (i...) { if
/// (payload[i] != expected) break; }` combines a plain, uniform trip-count
/// check (`i < N`, in its own block) with a separate, genuinely divergent
/// inner `break` check (`payload[i]` is a task-payload read, unconditionally
/// `NeverUniform` per `WaveUniformity.cpp`) -- once `UnifyLoopExits`/
/// `StructurizeCFG` restructure this, the trip-count check's own "exit" arm
/// ends up feeding a *literal-constant* incoming value into the very same
/// "Flow" merge block the divergent break check's own decision also feeds,
/// rather than reaching a merge block of its own the way
/// `foldRedundantFlowBlock`'s narrower, fully-constant shape expects.
///
/// Roadmap H94a: returns whether \p BB's own name matches one of the
/// synthetic relay-block naming conventions `StructurizeCFG`
/// (`"Flow"`, optionally with a disambiguating numeric suffix -- see
/// `FlowBlockName` in `llvm/lib/Transforms/Scalar/StructurizeCFG.cpp`) or
/// `llvm::ControlFlowUtils`' own exit-unification helper (a `".guard"`
/// suffix, similarly possibly followed by a numeric one) actually use for
/// the pure control-flow bookkeeping blocks they insert -- as opposed to
/// any real, user-authored block from the original shader source, which
/// this milestone's own scoping (`RelayTargets`/`RelayCandidates` below)
/// must never mistake for one of these. A naming-based check is
/// admittedly narrower than a fully structural one, but this file already
/// relies on these same literal conventions elsewhere (see
/// `matchExitCheckWithRelay`'s and `foldRedundantFlowBlock`'s own
/// comments), and scoping this milestone's own new, more powerful
/// collapse/merge capability to only what those upstream passes
/// themselves produced is the deliberately conservative choice here.
bool isSyntheticRelayBlockName(StringRef Name) {
  return Name.starts_with("Flow") || Name.contains(".guard");
}

/// Peels away only \p BB's constant-valued predecessor(s) whose own
/// terminator is a plain, single-successor unconditional branch into \p BB
/// (never one with its own side effects to reorder), redirecting each
/// straight to whichever of \p BB's own two successors that predecessor's
/// own constant selects -- bypassing \p BB entirely on that path -- while
/// leaving \p BB itself standing, still holding the genuinely divergent
/// decision for whichever predecessor(s) remain. Every one of \p BB's own
/// `phi`s may still be used beyond it (directly, relying on \p BB's
/// dominance, since \p BB used to be its only predecessor, or via an
/// existing downstream `phi`) -- `SSAUpdater` repairs any such use once the
/// peeled predecessor's edge no longer runs through \p BB. Returns whether
/// anything was peeled.
///
/// Deliberately narrower than a general jump-threading pass: it never
/// touches a predecessor with side-effecting instructions of its own
/// (besides its terminator), and only ever peels a predecessor whose
/// contribution to \p BB's *own* condition `phi` is a literal constant --
/// so, like `foldRedundantFlowBlock`, it can never mistake a genuine
/// divergent decision for a redundant one, only bypass a compile-time-
/// provable one.
bool peelConstantFlowPredecessors(BasicBlock *BB,
                                  SmallPtrSetImpl<BasicBlock *> &PeeledFrom,
                                  SmallPtrSetImpl<BasicBlock *> *RelayTargets = nullptr) {
  auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
  if (!Br)
    return false;
  bool Negated;
  Instruction *CondUser;
  PHINode *CondPN = getFlowConditionPhi(Br, BB, Negated, CondUser);
  if (!CondPN)
    return false;

  // Collect every incoming index whose value is a literal constant --
  // candidates to peel. If none are, there is nothing to do here; if
  // *every* one is, `foldRedundantFlowBlock` above already fully replaces
  // `BB`, more thoroughly, so leave that shape to it instead.
  SmallVector<unsigned, 2> PeelIndices;
  for (unsigned I = 0, E = CondPN->getNumIncomingValues(); I != E; ++I)
    if (isa<ConstantInt>(CondPN->getIncomingValue(I)))
      PeelIndices.push_back(I);
  if (PeelIndices.empty() || PeelIndices.size() == CondPN->getNumIncomingValues())
    return false;

  bool Changed = false;
  // Walk in reverse so removing an incoming value along the way never
  // invalidates a later index still to be processed.
  for (unsigned I : llvm::reverse(PeelIndices)) {
    BasicBlock *Pred = CondPN->getIncomingBlock(I);
    auto *PredBr = dyn_cast<UncondBrInst>(Pred->getTerminator());
    if (!PredBr || PredBr->getSuccessor(0) != BB)
      continue; // Not a plain, single-successor relay into `BB`.

    auto *K = cast<ConstantInt>(CondPN->getIncomingValue(I));
    bool ExitOnSucc0 = Negated ? !K->isOne() : K->isOne();
    BasicBlock *Target = ExitOnSucc0 ? Br->getSuccessor(0) : Br->getSuccessor(1);

    // Capture every one of `BB`'s own phi values along this predecessor's
    // edge before retargeting it, so `SSAUpdater` still has both the
    // original (`BB`'s phi) and the newly-available (this constant)
    // values to reconcile once the edge no longer runs through `BB`.
    SmallVector<std::pair<PHINode *, Value *>, 4> Incoming;
    for (PHINode &PN : BB->phis())
      Incoming.emplace_back(&PN, PN.getIncomingValueForBlock(Pred));

    // Roadmap L40: `Pred` is expected to be a plain, single-predecessor
    // critical-edge relay (`BreakCriticalEdges`'s own trampoline for the
    // edge a `CondBrInst` used to have straight into `BB`) rather than a
    // genuine decision of its own -- walk back to that real `CondBrInst`
    // origin (ordinarily just one hop) and record it: its own exit
    // decision is now *proven*, by this very peel, to be a compile-time-
    // redundant re-derivation of `BB`'s constant-selected outcome for this
    // predecessor, so callers should treat it as a "pass-through" check
    // rather than a candidate for `BB`'s own remaining, genuinely
    // divergent decision -- regardless of what `UniformityInfo` reports
    // for it (see `linearizeCycle`'s own comment on why that can still
    // conservatively call it divergent: it is fed, downstream, by a value
    // this same divergent join produces).
    BasicBlock *Origin = Pred;
    while (BasicBlock *Unique = Origin->getUniquePredecessor()) {
      if (isa<CondBrInst>(Origin->getTerminator()))
        break;
      Origin = Unique;
    }
    PeeledFrom.insert(Origin);

    // Roadmap H94a: `BB` itself (whose predecessor count this peel just
    // shrank) can be left with a single remaining real predecessor --
    // exactly the shape `collapseTriviallyRedundantPhisInCycle`/
    // `mergeTrivialRelayBlocksInCycle` below look for. Recording it here
    // (rather than considering *every* block in the cycle a candidate)
    // keeps that collapse narrowly scoped to blocks a peel just
    // structurally simplified -- but only when `BB` is itself one of
    // `StructurizeCFG`/`ControlFlowUtils`' own synthetic relay blocks
    // (`isSyntheticRelayBlockName`): a real, user-authored check block
    // (like a mesh shader's own genuinely divergent comparison) can
    // *also* end up with a single remaining predecessor this same way
    // (roadmap H89a/H89b's fused-uniform-check shape) without ever being
    // a redundant relay itself, and must never become eligible for this
    // milestone's later collapse/merge.
    if (RelayTargets && isSyntheticRelayBlockName(BB->getName()))
      RelayTargets->insert(BB);

    PredBr->setSuccessor(0, Target);
    Changed = true;

    for (auto &Pair : Incoming) {
      PHINode *PN = Pair.first;
      Value *V = Pair.second;
      SSAUpdater Updater;
      Updater.Initialize(PN->getType(), PN->getName());
      Updater.AddAvailableValue(BB, PN);
      Updater.AddAvailableValue(Pred, V);
      // Roadmap H89b: a use of `PN` *inside* `BB` itself (most notably
      // `BB`'s own terminator, when `PN` is the condition `phi` this very
      // function is peeling a predecessor of) is still trivially
      // dominated by `PN`'s definition at the top of `BB` -- nothing
      // about that reachability changes just because one of `BB`'s
      // *other* predecessors is being bypassed. `SSAUpdater::RewriteUse`
      // must not be asked to rewrite it: once `AddAvailableValue(BB, PN)`
      // makes `HasValueForBlock(BB)` true, `GetValueInMiddleOfBlock`
      // takes its "value redefined partway through the block" reconciler
      // path instead of returning `PN` directly -- and, since this
      // peeling only ever registers a value for `BB` and for `Pred`
      // (never for `BB`'s *other*, still-genuine predecessors, whose
      // real incoming value it has no reason to know), that reconciler
      // cannot find one for them either and silently synthesizes brand
      // new, semantically-bogus `phi`s while walking back through the
      // rest of the cycle (as far as the loop header) trying to invent
      // one -- corrupting the very value this peel is supposed to leave
      // untouched. Only a use genuinely outside `BB` (where the edge
      // being peeled really can change which value reaches it) needs
      // `SSAUpdater`'s help at all.
      for (Use &U : llvm::make_early_inc_range(PN->uses())) {
        auto *UserInst = cast<Instruction>(U.getUser());
        auto *UserPN = dyn_cast<PHINode>(UserInst);
        BasicBlock *UserBlock =
            UserPN ? UserPN->getIncomingBlock(U) : UserInst->getParent();
        if (UserBlock == BB)
          continue;
        Updater.RewriteUse(U);
      }
    }
    for (PHINode &PN : BB->phis())
      PN.removeIncomingValue(Pred, /*DeletePHIIfEmpty=*/false);
  }
  return Changed;
}

/// Repeatedly applies `peelConstantFlowPredecessors` to every block \p C
/// contains besides \p Header/\p Latch until none match, collecting every
/// "pass-through" origin block discovered along the way into \p PeeledFrom.
/// Roadmap L40. See `foldRedundantFlowBlocksInCycle`'s comment; the two
/// folds are complementary (a block with every incoming value constant is
/// fully eliminated by that one instead) and safely order-independent.
bool peelConstantFlowPredecessorsInCycle(CycleInfo &CI, CycleRef C,
                                         BasicBlock *Header, BasicBlock *Latch,
                                         SmallPtrSetImpl<BasicBlock *> &PeeledFrom,
                                         SmallPtrSetImpl<BasicBlock *> *RelayTargets = nullptr) {
  bool Changed = false;
  bool PeeledThisPass = true;
  while (PeeledThisPass) {
    PeeledThisPass = false;
    for (BasicBlock &BB : *Header->getParent()) {
      if (!CI.contains(C, &BB) || &BB == Header || &BB == Latch)
        continue;
      if (peelConstantFlowPredecessors(&BB, PeeledFrom, RelayTargets)) {
        Changed = PeeledThisPass = true;
        break; // A predecessor edge changed; restart the scan to be safe.
      }
    }
  }
  return Changed;
}

/// Roadmap H94a: once `peelConstantFlowPredecessorsInCycle` has bypassed a
/// block's only *constant*-valued predecessor edge, `StructurizeCFG`'s own
/// "Flow" reconvergence scheme can leave that block with exactly ONE
/// predecessor remaining -- at which point every one of its own `phi`s is
/// trivially just that predecessor's value (see `lookThroughTrivialPhi`'s
/// own comment for why a single-incoming-value, or otherwise identical-
/// valued-on-every-edge, `phi` is always safe to replace outright: its
/// resolved value necessarily already dominates the `phi`'s own block).
/// Reduced from a real `dEQP-VK.mesh_shader.ext.properties.
/// mesh_shared_memory_size` verification loop's own `Flow` block, left
/// with a single predecessor (`Flow24`) and four now-trivial `phi`s once
/// its own other, constant-valued predecessor is peeled away -- none of
/// which `foldRedundantFlowBlock`/`peelConstantFlowPredecessors` above can
/// yet see through, since neither looks past a `phi`'s own immediate
/// incoming values.
///
/// Collapses any such trivially-redundant `phi` in the cycle by replacing
/// every use with its resolved value and erasing it. Always sound (see
/// above); deliberately narrower than a general `SimplifyCFG`-style pass,
/// touching only `phi`s `lookThroughTrivialPhi` itself already proves
/// redundant -- and, further, only within \p RelayCandidates (see
/// `peelConstantFlowPredecessors`'s own `RelayTargets` comment): a real,
/// user-authored block's own exit-check `phi` can *also* become
/// single-incoming after an unrelated peel (e.g. a plain uniform
/// trip-count check fused into it, roadmap H89a/H89b), but collapsing
/// *that* one away would erase the very check this milestone's later
/// classification (`OtherCondBrBlocks`) and its own masking logic depend
/// on finding intact -- restricting to blocks a peel's own redirected
/// edge just produced keeps this pass from ever touching one.
///
/// Contrast this with the earlier, unsound same-milestone attempt (see the
/// "H94 session" note in agent_thoughts.md) that tried to fold away a
/// `phi` with just one *non-poison* incoming value among several
/// *differently*-sourced ones: that shape's real operand does not
/// generally dominate the `phi`'s own block, since other predecessors may
/// reach it without passing through the real operand's own defining block.
/// `lookThroughTrivialPhi` never matches that shape at all -- it requires
/// every non-self incoming value (there may be only one) to be the
/// identical `Value`, which by ordinary SSA dominance rules already
/// guarantees that value dominates every one of the `phi`'s own
/// predecessors, hence the `phi` itself.
bool collapseTriviallyRedundantPhisInCycle(
    CycleInfo &CI, CycleRef C, BasicBlock *Header, BasicBlock *Latch,
    const SmallPtrSetImpl<BasicBlock *> &RelayCandidates) {
  bool Changed = false;
  bool CollapsedThisPass = true;
  while (CollapsedThisPass) {
    CollapsedThisPass = false;
    for (BasicBlock &BB : *Header->getParent()) {
      if (!CI.contains(C, &BB) || !RelayCandidates.contains(&BB))
        continue;
      for (PHINode &PN : BB.phis()) {
        Value *Resolved = lookThroughTrivialPhi(&PN);
        if (Resolved == &PN)
          continue; // Not trivially redundant.
        PN.replaceAllUsesWith(Resolved);
        PN.eraseFromParent();
        Changed = CollapsedThisPass = true;
        break; // `BB.phis()` iterator invalidated; restart this block.
      }
      if (CollapsedThisPass)
        break; // Restart the whole scan too: `Resolved` may live
               // elsewhere in the cycle and become collapsible in turn.
    }
  }
  return Changed;
}

/// Returns whether every instruction in \p BB is either a `PHINode` or its
/// own terminator -- i.e. \p BB carries no real computation of its own at
/// all, only control-flow bookkeeping. Roadmap H94a:
/// `mergeTrivialRelayBlocksInCycle` below only ever merges a relay block
/// into a predecessor matching this shape, so it can never absorb a real,
/// semantically meaningful block (like a genuine divergent check's own
/// block computing an actual comparison) into a relay chain, even when
/// that real block happens to also end up with a single predecessor and
/// no `phi`s of its own.
bool isPureRelayBlock(const BasicBlock *BB) {
  for (const Instruction &I : *BB)
    if (!isa<PHINode>(I) && &I != BB->getTerminator())
      return false;
  return true;
}

/// Roadmap H94a: `collapseTriviallyRedundantPhisInCycle` above can leave a
/// block with no `phi`s of its own at all, and exactly one predecessor
/// whose own sole successor is that same block -- a pure, now-empty-of-
/// decisions relay `StructurizeCFG` built, structurally identical to
/// `llvm::MergeBlockIntoPredecessor`'s own target shape. Merging it into
/// that predecessor moves its own `CondBrInst` terminator (whatever
/// condition it now directly uses, unblocked from the relay's own
/// `phi`s) into the predecessor's block instead -- exposing
/// `foldRedundantFlowBlock`'s already-existing fully-constant-`phi`
/// pattern one level further back up the relay chain than it could
/// previously see, without this function itself needing to understand
/// anything about *why* the relay was redundant.
///
/// Deliberately restricted to \p RelayCandidates (see
/// `collapseTriviallyRedundantPhisInCycle`'s own comment for why: a real
/// block should never be absorbed into a relay chain, even a phi-less
/// one), to predecessors additionally proven to be pure relay blocks in
/// their own right (`isPureRelayBlock`, defense in depth against ever
/// merging into a real block), never `Header`/`Latch` (which have their
/// own, separately-handled roles), and to
/// `llvm::MergeBlockIntoPredecessor`'s own default (single-successor
/// predecessor) case -- this never needs the two-successors variant here,
/// since a genuine `StructurizeCFG` relay's own predecessor is, by
/// construction, itself a redundant single-successor pass-through.
/// Returns whether anything was merged; \p RelayCandidates gains the
/// surviving, merged-into predecessor so a longer relay chain can keep
/// collapsing across further fixed-point iterations.
bool mergeTrivialRelayBlocksInCycle(
    CycleInfo &CI, CycleRef C, BasicBlock *Header, BasicBlock *Latch,
    SmallPtrSetImpl<BasicBlock *> &RelayCandidates) {
  bool Changed = false;
  bool MergedThisPass = true;
  while (MergedThisPass) {
    MergedThisPass = false;
    for (BasicBlock &BB : *Header->getParent()) {
      if (!CI.contains(C, &BB) || &BB == Header || &BB == Latch ||
          !RelayCandidates.contains(&BB))
        continue;
      if (!BB.phis().empty())
        continue; // Still carries real, unresolved values of its own.
      BasicBlock *Pred = BB.getUniquePredecessor();
      if (!Pred || !CI.contains(C, Pred) || Pred == Header || Pred == Latch ||
          !isPureRelayBlock(Pred))
        continue;
      if (MergeBlockIntoPredecessor(&BB)) {
        RelayCandidates.insert(Pred);
        Changed = MergedThisPass = true;
        break; // `BB` itself is erased; restart the scan to be safe.
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
  Result.RelayBlock = &BB;
  return Result;
}

std::optional<LoopLinearizer::ExitCheck>
LoopLinearizer::matchExitCheckWithRelay(BasicBlock &BB,
                                        BasicBlock *ExitBlock) {
  if (std::optional<ExitCheck> Direct = matchExitCheck(BB, ExitBlock))
    return Direct;
  auto *Br = dyn_cast<CondBrInst>(BB.getTerminator());
  if (!Br)
    return std::nullopt;

  std::optional<ExitCheck> Result;
  for (unsigned I = 0; I != 2; ++I) {
    BasicBlock *Candidate = Br->getSuccessor(I);
    if (!straightChain(Candidate, ExitBlock))
      continue;
    if (Result)
      return std::nullopt; // Both arms reach it: ambiguous.
    ExitCheck EC;
    EC.Br = Br;
    EC.Cond = Br->getCondition();
    EC.ExitOnTrue = (I == 0);
    EC.StayInLoop = Br->getSuccessor(1 - I);
    EC.RelayBlock = Candidate;
    Result = EC;
  }
  return Result;
}

std::optional<SmallPtrSet<BasicBlock *, 8>>
LoopLinearizer::collectUniformPassThroughRegion(
    BasicBlock *From, BasicBlock *To, BasicBlock *ExitBlock, CycleRef C,
    UniformityInfo &UI, const SmallPtrSetImpl<BasicBlock *> &PeeledFrom) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  SmallVector<BasicBlock *, 8> Worklist{From};
  while (!Worklist.empty()) {
    BasicBlock *Cur = Worklist.pop_back_val();
    if (Cur == To || Cur == ExitBlock)
      continue;
    if (!Visited.insert(Cur).second)
      continue; // Already queued/visited via another arm.
    if (!CI.contains(C, Cur))
      return std::nullopt; // Escaped the cycle without ever reaching `To`.
    if (auto *UBr = dyn_cast<UncondBrInst>(Cur->getTerminator())) {
      Worklist.push_back(UBr->getSuccessor(0));
      continue;
    }
    auto *CBr = dyn_cast<CondBrInst>(Cur->getTerminator());
    if (!CBr)
      return std::nullopt; // Not a shape this milestone understands.
    // A further, genuinely divergent branch strictly *inside* this
    // region (rather than being `To` itself) is the two-divergent-check
    // shape this milestone does not yet support -- `PeeledFrom` wins the
    // same way it does everywhere else in this pass (see
    // `linearizeCycle`'s own comment).
    if (!PeeledFrom.contains(Cur) && UI.isDivergentTerminator(CBr))
      return std::nullopt;
    Worklist.push_back(CBr->getSuccessor(0));
    Worklist.push_back(CBr->getSuccessor(1));
  }
  return Visited;
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

  // Roadmap L40: a separate, plain uniform check's own "exit" arm can
  // instead end up fused into another, genuinely divergent check's own
  // merge block as just one of its `phi`'s incoming values (rather than
  // being fully redundant, like the shape H19k's fold above targets, or
  // reaching a merge block of its own) -- see
  // `peelConstantFlowPredecessors`'s own comment. Decouples that so the
  // uniform check can be recognized as its own "pass-through" block below
  // (see `collectUniformPassThroughRegion`) rather than an unrecognized
  // second `OtherCondBrBlocks` entry. `PeeledFrom` records every such
  // uniform check's own block: `UniformityInfo` cannot be trusted to
  // (re-)classify it as non-divergent even after this peel (see the next
  // comment), so the classification below trusts this explicit, structural
  // proof
  // instead wherever it applies.
  SmallPtrSet<BasicBlock *, 2> PeeledFrom;
  SmallPtrSet<BasicBlock *, 2> RelayCandidates;
  peelConstantFlowPredecessorsInCycle(CI, C, Header, Latch, PeeledFrom,
                                      &RelayCandidates);

  // Roadmap H94a: peeling above can leave a relay block with a single
  // remaining predecessor and only trivially-redundant `phi`s of its own
  // (see `collapseTriviallyRedundantPhisInCycle`'s own comment) --
  // collapsing those and merging the resulting phi-less relay into its
  // predecessor (`mergeTrivialRelayBlocksInCycle`) can expose a *further*
  // fully-constant-`phi` `CondBrInst` one level back, which the two folds
  // above did not get a chance to see the first time around. Re-running
  // all four to a fixed point handles a relay chain of any length this
  // way, one hop at a time, rather than needing its own N-hop-aware
  // recognizer. `RelayCandidates` (seeded, and grown, by the two peel/
  // merge steps themselves) keeps the two new steps scoped to blocks a
  // peel or merge actually produced, never a genuine, semantically
  // meaningful block this milestone must leave alone.
  bool CollapsedRelay = true;
  while (CollapsedRelay) {
    CollapsedRelay = collapseTriviallyRedundantPhisInCycle(CI, C, Header,
                                                           Latch,
                                                           RelayCandidates);
    CollapsedRelay |= mergeTrivialRelayBlocksInCycle(CI, C, Header, Latch,
                                                     RelayCandidates);
    if (CollapsedRelay) {
      foldRedundantFlowBlocksInCycle(CI, C, Header, Latch);
      peelConstantFlowPredecessorsInCycle(CI, C, Header, Latch, PeeledFrom,
                                          &RelayCandidates);
    }
  }

  // Roadmap L40: `UniformityInfo` (recomputed fresh below, now that
  // peeling has already happened) still, correctly, reports a "pass-
  // through" block like `check` (peeled from above) as divergent: once a
  // loop has *any* genuinely divergent exit at all, its own induction
  // variable becomes divergent too (different lanes really do complete a
  // different number of iterations in the current, not-yet-linearized
  // structured CFG -- linearization is precisely what removes that real
  // divergence, by continuing every lane's iteration count uniformly
  // instead), so *every* later use of it -- including an entirely
  // ordinary, separate `i < N` trip-count comparison -- is genuinely,
  // correctly divergent by `UniformityInfo`'s own flow-insensitive,
  // per-value model. That model has no way to single out `Flow`'s own
  // decision as "the" original source and `check`'s as merely a
  // downstream user of a value entangled with it -- `PeeledFrom` (a
  // structural, not a uniformity-based, proof) is what actually
  // distinguishes them below. See this class's own `UI` member comment
  // for why that pre-mutation `UniformityInfo` (not a fresh recompute
  // against this cycle's now-mutated `CI`) is exactly what is needed here.

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
    // Roadmap L40: a real, uniform exit check already sitting in the
    // header and/or the latch (an ordinary `for`/`while` trip-count test,
    // left completely untouched below exactly as the whole-uniform-loop
    // case at the bottom of this function leaves one) may legitimately
    // coexist with a single, separate divergent exit check elsewhere in
    // the loop body -- a real `dEQP-VK.mesh_shader.ext.misc.payload_read`
    // shader's own `for (i...) { if (payload[i] != expected) break; }`
    // verification loop takes exactly this shape: `payload[i]` is a task-
    // payload read, unconditionally `NeverUniform` per
    // `WaveTTIImpl::getValueUniformity` (see WaveUniformity.cpp), so the
    // `if`'s own break check is always treated as divergent regardless of
    // whether every lane's data agrees in practice, while the loop's own
    // `i < N` trip count is a plain, genuinely uniform comparison in a
    // separate block -- one that, after `StructurizeCFG`/`UnifyLoopExits`
    // restructure the loop, can itself land among `OtherCondBrBlocks`
    // rather than in `Header`/`Latch` directly (see
    // `peelConstantFlowPredecessors`'s own comment on why its own "exit"
    // arm needs decoupling first). Only a *divergent* header/latch exit
    // check coexisting with another divergent check elsewhere is the
    // genuinely harder two-divergent-exit shape this milestone does not
    // yet support.
    if (HeaderDivergent || LatchDivergent) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" +
                      OtherCondBrBlocks.front()->getName() +
                      "'; unsupported (roadmap milestone 6 deviation)");
      return false;
    }

    // Find the single, genuinely divergent block among
    // `OtherCondBrBlocks` -- any other entry here must instead be a
    // separate, non-divergent "pass-through" block (like `Header`/
    // `Latch` above), tolerated (left completely untouched) by
    // `collectUniformPassThroughRegion` below rather than treated as this
    // cycle's own real check. `PeeledFrom` (see above) always wins this
    // classification over `UI.isDivergentTerminator` when it applies: a
    // block already structurally proven redundant by the peel is never a
    // pass-through/real-check ambiguity `UniformityInfo` needs to
    // resolve.
    //
    // Roadmap L42: classification here is driven by actual divergence
    // (`UI.isDivergentTerminator`), not by whether a block happens to
    // match `matchExitCheckWithRelay`'s narrower shape -- a genuinely
    // uniform pass-through block (like the outer, uniform trip-count
    // check in a real `dEQP-VK.mesh_shader.ext.misc.payload_read`
    // shader's own verification loop) need not itself look anything like
    // an exit check at all: its own "skip the body" arm can rejoin the
    // real divergent check's own `StructurizeCFG`-built merge block
    // directly, never reaching `ExitBlock` via any straight or singly-
    // relayed chain -- see `collectUniformPassThroughRegion`'s own
    // comment for the region walk that recognizes it instead.
    SmallVector<BasicBlock *, 2> DivergentCandidates;
    for (BasicBlock *BB : OtherCondBrBlocks)
      if (!PeeledFrom.contains(BB) && UI.isDivergentTerminator(BB->getTerminator()))
        DivergentCandidates.push_back(BB);

    if (DivergentCandidates.size() > 1) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has more than one divergent exit check ('" +
                      DivergentCandidates[0]->getName() + "' and '" +
                      DivergentCandidates[1]->getName() +
                      "'); unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    if (DivergentCandidates.empty())
      return false; // No divergence anywhere here either: leave alone.

    BasicBlock *CheckBlock = DivergentCandidates.front();
    std::optional<ExitCheck> CheckExit =
        matchExitCheckWithRelay(*CheckBlock, ExitBlock);
    if (!CheckExit) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + CheckBlock->getName() +
                      "' that does not reach the loop's exit block; "
                      "unsupported (roadmap milestone 6 deviation)");
      return false;
    }

    std::optional<SmallPtrSet<BasicBlock *, 8>> PreRegion =
        collectUniformPassThroughRegion(Header, CheckBlock, ExitBlock, C, UI,
                                        PeeledFrom);
    std::optional<SmallPtrSet<BasicBlock *, 8>> PostRegion =
        collectUniformPassThroughRegion(CheckExit->StayInLoop, Latch,
                                        ExitBlock, C, UI, PeeledFrom);
    if (!PreRegion || !PostRegion) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + CheckBlock->getName() +
                      "'; only a uniform pass-through region to/from the "
                      "exit check is supported yet (roadmap milestone 6 "
                      "deviation)");
      return false;
    }
    // Every other `OtherCondBrBlocks` entry must be accounted for by one
    // of these two regions -- anything left over is a genuinely
    // unsupported shape (e.g. a second real internal branch that is
    // neither a uniform pass-through nor this cycle's own check).
    for (BasicBlock *BB : OtherCondBrBlocks) {
      if (BB == CheckBlock || PreRegion->contains(BB) ||
          PostRegion->contains(BB))
        continue;
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" + BB->getName() +
                      "' that does not reach the loop's exit block; "
                      "unsupported (roadmap milestone 6 deviation)");
      return false;
    }

    MaskPair Masks = makeActivePNPair();
    for (BasicBlock *BB : *PreRegion)
      applyStageMasks(*BB, Masks);
    applyStageMasks(*CheckBlock, Masks);

    // Roadmap H94a: capture, for every one of `ExitBlock`'s own phis
    // (besides the live/side-effect masks -- see `addLatchIncoming`
    // below), whatever value `CheckExit->RelayBlock` itself contributes,
    // *before* it is potentially removed as a predecessor just below --
    // this milestone's own "never really exit here, defer to Latch"
    // strategy is about to make `Latch` a brand-new predecessor of
    // `ExitBlock` too (see the `CondBrInst::Create` below), and the
    // semantically correct value for any such leftover phi to carry
    // along that new edge is exactly the one this cycle's own real exit
    // decision already associated with actually reaching `ExitBlock` --
    // i.e., `RelayBlock`'s own contribution, not some arbitrary or
    // undefined value.
    SmallVector<std::pair<PHINode *, Value *>, 4> ExitBlockRelayValues;
    for (PHINode &PN : ExitBlock->phis())
      if (int Idx = PN.getBasicBlockIndex(CheckExit->RelayBlock); Idx != -1)
        ExitBlockRelayValues.emplace_back(&PN, PN.getIncomingValue(Idx));

    IRBuilder<> CheckBuilder(CheckExit->Br);
    Value *Staying = CheckExit->ExitOnTrue
                         ? CheckBuilder.CreateNot(CheckExit->Cond)
                         : CheckExit->Cond;
    MaskPair MasksAfterCheck =
        stayInLoop(CheckBuilder, Masks, Staying, "active.check");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see "Loops with a
    // divergent exit" below).
    // Roadmap L40: `CheckExit`'s own edge to `ExitBlock` (when matched
    // directly rather than via a relay -- see `matchExitCheck`) has just
    // vanished from the CFG entirely (the lane that would have taken it
    // instead continues, masked, toward the latch): repair any of
    // `ExitBlock`'s own phis that still list `CheckBlock` as an incoming
    // block accordingly. Genuinely a no-op when the match was instead via
    // a relay (see `matchExitCheckWithRelay`), since then `CheckBlock`
    // itself was never really one of `ExitBlock`'s own listed
    // predecessors to begin with -- the (now merely dead, but still
    // syntactically valid) relay chain's own last hop was -- hence the
    // explicit membership check: `removePredecessor` itself asserts its
    // argument already is a real predecessor, rather than silently
    // tolerating one that never was.
    //
    // `KeepOneInputPHIs=true` is required here: `ExitBlockRelayValues`
    // just captured raw `PHINode *` pointers above, and
    // `removePredecessor`'s *default* behavior (`KeepOneInputPHIs=false`)
    // is to eagerly RAUW-and-erase any phi that this removal leaves with
    // only one remaining incoming value -- exactly the shape a phi with
    // only `CheckBlock` and `CheckExit->RelayBlock` as its two
    // predecessors is in. Without this, those captured pointers can be
    // left dangling by the time the restore loop below dereferences them
    // (a real, reproduced use-after-free crash during this milestone's
    // own development).
    if (llvm::is_contained(predecessors(ExitBlock), CheckBlock))
      ExitBlock->removePredecessor(CheckBlock, /*KeepOneInputPHIs=*/true);
    UncondBrInst::Create(CheckExit->StayInLoop, CheckExit->Br->getIterator());
    CheckExit->Br->eraseFromParent();

    for (BasicBlock *BB : *PostRegion)
      applyStageMasks(*BB, MasksAfterCheck);
    applyStageMasks(*Latch, MasksAfterCheck);

    Value *Continue = closeLatch(Latch, Header, MasksAfterCheck);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
    // Roadmap H94a: `Latch` just became a brand-new predecessor of
    // `ExitBlock` (unlike the `HeaderDivergent`/`LatchDivergent` cases
    // below, where `Latch` already targeted `ExitBlock` directly before
    // this transform) -- restore the value each of `ExitBlock`'s own
    // leftover phis captured above for this edge too, so every one of
    // `ExitBlock`'s phis still lists exactly one entry per real
    // predecessor.
    for (auto &[PN, V] : ExitBlockRelayValues)
      if (PN->getBasicBlockIndex(Latch) == -1)
        PN->addIncoming(V, Latch);
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
    // Roadmap L40: see the identical `CheckBlock` case's own comment above
    // -- `Header`'s own edge straight to `ExitBlock` has just vanished
    // from the CFG the same way; repair any of `ExitBlock`'s own phis
    // that still list it as an incoming block accordingly.
    ExitBlock->removePredecessor(Header);
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
    // invalidated `DT`/`CI`; `PostDominatorTree` was already block-scoped
    // above). Loops are structurally untouched by it, but recompute fresh
    // regardless, since it is cheap next to getting this wrong.
    // Roadmap L40: `LoopLinearizer` takes this single, whole-function
    // `UniformityInfo` (computed once here, before any cycle is
    // linearized) and deliberately never recomputes its own -- see the
    // `UI` member's own comment on why recomputing one per cycle, against
    // that cycle's own already-mutated `CycleInfo`, is unsound (it
    // previously crashed on a real, nested-loop shader).
    DominatorTree DT2(F);
    CycleInfo CI2;
    CI2.compute(F);
    UniformityInfo UI2 = computeWaveUniformity(F, DT2, CI2);
    bool CycleChanged = LoopLinearizer(F, CI2, UI2).run();
    // Roadmap H94b: eliminating a loop's divergent exit `CondBr` in favor
    // of an unconditional fall-through plus a mask computation (this
    // pass's whole point) can leave one of that `CondBr`'s own successors
    // -- typically a `StructurizeCFG`-built critical-edge relay stub that
    // had no other predecessor -- entirely unreachable. Left behind, a
    // later, stricter consumer of this pass's output
    // (`feme::cpu::EntryWrapperPass`'s own `isLinearChain`, which requires
    // every block in the function to be accounted for) can spuriously
    // reject an otherwise-fully-supported shape purely because of this
    // dead residue. Clean it up here, right after producing it, rather
    // than expecting every downstream consumer to tolerate or work around
    // dead blocks this pass itself introduced.
    if (CycleChanged)
      EliminateUnreachableBlocks(F);
    Changed |= CycleChanged;

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
