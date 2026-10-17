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
#include "feme/Transforms/CPU/WaveCalls.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Forward declaration -- real definition (and its own full rationale
/// comment) lives alongside `mergeTrivialRelayBlocksInCycle`'s own
/// helpers, much later in this file; `DiamondFlattener::
/// isLoopControlEdgeThroughRelay` (see its own comment) needs it earlier.
bool isPureRelayBlock(const BasicBlock *BB);

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

/// Roadmap L440: a `WeakTrackingVH`-backed counterpart to `MaskPair`, used
/// only for caches whose entries must outlive the mutation of unrelated
/// IR between the point they are recorded and the point they are read
/// back (`DiamondFlattener::CycleBoundaryMasks`/`LoopLinearizer::
/// DiamondFlattenedEntryMasks`). A plain `MaskPair` stored in such a
/// cache is a raw, untracked `Value *`: if anything erases the
/// instruction it refers to in between (e.g. `LoopLinearizer::run()`
/// linearizing one cycle can erase blocks/values referenced by a
/// `MaskPair` recorded for a *different*, not-yet-processed cycle), the
/// cached pointer silently dangles, and the read site threads that
/// dangling pointer straight into a new `PHINode`'s incoming-value list
/// as a real `Use` -- a genuine use-after-free (confirmed to reproduce,
/// non-deterministically under ASLR, via `WaveReadLaneFirst.fp16.test`).
/// `WeakTrackingVH` participates in LLVM's `ValueHandleBase` tracking:
/// it auto-updates across a RAUW (so it never goes stale that way) and
/// auto-nulls (rather than dangling) once the value it tracks is really
/// erased, so a consumer can safely detect and gracefully fall back
/// instead of reading freed memory.
struct WeakMaskPair {
  WeakTrackingVH Live;
  WeakTrackingVH SideEffect;

  WeakMaskPair() = default;
  WeakMaskPair(const MaskPair &MP) : Live(MP.Live), SideEffect(MP.SideEffect) {}

  /// Whether both tracked values are still present, i.e. neither has been
  /// erased since this entry was recorded. A RAUW'd-but-still-live value
  /// is fine (the handle follows it); only an erasure nulls it out.
  bool isValid() const { return Live && SideEffect; }

  /// Materializes the currently-tracked values back into a plain
  /// `MaskPair`. Only meaningful when `isValid()` -- a caller that cannot
  /// guarantee the entry is still fresh must check that first.
  MaskPair get() const { return {Live, SideEffect}; }
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

/// (Roadmap L419(w)) Narrows a `feme.cpu.resource.*`/`feme.cpu.image.*`
/// call's trailing mask operand (at \p MaskArgIdx) to \p Mask, but only
/// when doing so can only ever *narrow* (never *widen* or otherwise
/// discard) whatever masking the call already carries: skips the
/// overwrite when \p Call's own current mask operand is already known
/// non-constant (i.e. some earlier pass -- typically an enclosing
/// `DiamondFlattener::flatten()` walk that ran over this same call
/// before `LoopLinearizer` ever began processing its own cycle -- has
/// already narrowed it to something genuinely branch/iteration-
/// dependent), regardless of whether \p Mask itself is currently known
/// constant. An earlier version of this check only ever looked at
/// \p Mask's own constant-ness (unconditionally overwriting the call's
/// existing operand whenever \p Mask looked non-constant) -- correct as
/// long as every call this ever ran on still carried
/// `SPIRVResourceLoweringPass`'s own original, universally-constant
/// `true` placeholder at that point, but not once `LoopLinearizer`'s own
/// `poison`-seeded `active.live`/`active.sideeffect` phis (see
/// `LoopLinearizer::linearizeCycle`'s own `makeActivePNPair` comment)
/// made \p Mask *look* non-constant -- via `isKnownConstantMask`'s
/// `lookThroughTrivialPhi`, which (correctly) cannot yet tell a
/// `poison` placeholder's eventual real value apart from a genuinely
/// divergent one -- for a cycle whose masking will, once fully resolved,
/// actually turn out trivially uniform after all. That premature "looks
/// non-constant" state let this function clobber a call's already-
/// correct, `DiamondFlattener`-narrowed mask (e.g. an outer divergent
/// diamond's own `%sideeffect.t`) with the cycle's own, structurally
/// different (if ultimately equivalent) `active.sideeffect` phi --
/// discarding the outer diamond's own condition dependency outright, a
/// real regression caught by `PromotesUniformLatchCheckBehindTrivialRelay`.
/// Requiring the call's own *current* operand to itself be a known
/// constant before ever overwriting it keeps this function's original,
/// still-necessary behavior (narrowing a still-untouched `true`
/// placeholder once `Mask` genuinely becomes non-constant) while never
/// discarding a narrower mask some earlier walk already installed.
static void narrowCallMask(CallInst *Call, unsigned MaskArgIdx, Value *Mask) {
  if (!isKnownConstantMask(Mask) &&
      isKnownConstantMask(Call->getArgOperand(MaskArgIdx)))
    Call->setArgOperand(MaskArgIdx, Mask);
}

/// (Roadmap L419(w)) Folds each of \p MaskPhis -- `LoopLinearizer::
/// makeActivePNPair`'s own `active.live`/`active.sideeffect` phis, just
/// after `addLatchIncoming` has filled in their real backedge values --
/// back down to a single shared value if every incoming edge (now that
/// all of them are finally known) genuinely agrees, propagating that
/// fold through any instruction it makes foldable in turn (an `and`/
/// `xor`/`select` one of whose operands just became that shared value,
/// or a downstream "merge" phi some uniform diamond's own mask-threading
/// built purely to carry a mask phi through unchanged). Unlike
/// `isKnownConstantMask`'s own `lookThroughTrivialPhi` helper -- which
/// must run *before* `addLatchIncoming` for cases that need it (see the
/// poison-placeholder seed's own comment) and so can only ever see these
/// phis in their still-incomplete state -- this runs once they are known
/// complete, so it can tell a genuinely still-divergent cycle (whose
/// backedge value differs from its entry value, e.g.
/// `select_iteration_count`'s own masking) apart from one whose masking
/// turned out never to need to become non-constant at all (e.g. a loop
/// with no internal break/discard/demote at all, whose backedge value is
/// trivially the same `true` its entry value already was) -- safely,
/// without the false-positive risk `lookThroughTrivialPhi` has on an
/// incomplete phi.
///
/// Deliberately scoped to exactly \p MaskPhis' own transitive operand
/// closure (restricted to \p C's own blocks, via \p CI's membership
/// test) -- *not* every phi/instruction in the cycle. An earlier version
/// of this swept the whole cycle and, while it correctly folded the mask
/// machinery, also collapsed unrelated single-input "relay" phis other
/// code elsewhere in `LoopLinearizer`/`DiamondFlattener` depends on
/// keeping as genuine phis for its own structural reasons (see
/// `loop-uniform-check-fused-with-divergent-exit-inblock-use.ll`'s own
/// `%cond` phi, corrupted that way during this fix's own development) --
/// a mask phi's backedge value is often not literally the same `Value`
/// as its non-backedge one directly, but some other "merge" phi a
/// uniform diamond's own mask-threading built in between purely to carry
/// it through unchanged (e.g. `DiamondFlattener::flatten`'s own uniform-
/// diamond merge code, which unconditionally builds `phi [%active.live,
/// Then], [%active.live, Else]` regardless of whether that merge is
/// actually needed) -- that intermediate phi must itself be seen to
/// trivially collapse to the mask phi (both its own incoming edges
/// already, always, agree, independent of the outer phi's own fold)
/// *before* the outer phi's own remaining, now-self-referential backedge
/// can be recognized as safe to ignore; walking the closure once up
/// front (rather than scanning every instruction in the cycle) is what
/// keeps this fix from reaching values with no real connection to
/// \p MaskPhis at all. Runs to a local fixed point for the same reason:
/// one fold can expose another not reachable by a simple one-directional
/// worklist starting from a mask phi's own users alone (the
/// intermediate-merge-phi case above is discovered the *other*
/// direction, from the phi's own incoming values, not its users).
/// (Roadmap L419(w)) \p MaskPhis itself may be among the instructions
/// this function folds away (e.g. when a cycle's masking turns out never
/// to need to become non-constant at all, `Masks.Live`'s own phi is
/// exactly as foldable as everything else in its closure) -- every
/// caller's own `Masks.Live`/`Masks.SideEffect` raw `Value *` fields
/// still point at that now-`eraseFromParent`'d `PHINode` once this
/// function returns unless corrected, a real, reproduced dangling-
/// pointer crash (nondeterministic by nature -- the freed `PHINode`'s
/// memory is simply reused by whatever the optimizer allocates next,
/// surfacing as a segfault anywhere from a later `verifyModule` call to
/// `feme::cpu::SIMDizePass` several stages later, depending entirely on
/// allocator reuse timing) found running `dEQP-VK.glsl.loops.special.
/// do_while_constant_iterations.select_iteration_count_fragment`
/// through the real compiler pipeline -- `check-feme`'s own lit/unit
/// tests never actually exercise a shape where `Masks.Live`'s own phi
/// gets folded here, only ones where some *other* instruction in its
/// closure does. Returns, for each of \p MaskPhis (same order), the
/// final value it was folded to (chasing through any further fold that
/// final value itself was also subject to -- see this function's own
/// "fixed point" comment below), or that same `PHINode *` unchanged if
/// it was never folded at all -- every caller must use *this* function's
/// return value in place of its own prior raw pointer to \p MaskPhis'
/// members from this point on, precisely because that prior pointer may
/// no longer be safe to dereference.
static SmallVector<Value *, 2>
foldNowTriviallyConstantMaskPhis(CycleInfo &CI, CycleRef C,
                                 const DataLayout &DL,
                                 ArrayRef<PHINode *> MaskPhis) {
  SmallPtrSet<Instruction *, 16> InScope;
  SmallVector<Instruction *, 16> Worklist;
  for (PHINode *PN : MaskPhis)
    if (InScope.insert(PN).second)
      Worklist.push_back(PN);
  for (unsigned Idx = 0; Idx < Worklist.size(); ++Idx) {
    for (Value *Op : Worklist[Idx]->operands())
      if (auto *OpI = dyn_cast<Instruction>(Op))
        if (CI.contains(C, OpI->getParent()) &&
            // Roadmap L419(w): a single-incoming-edge phi is *not* the
            // "transparent merge" shape this fold exists to chase
            // through (e.g. `%live.merge = phi [%active.live, Then],
            // [%active.live, Else]`, always exactly 2+ incoming edges,
            // all structurally the same value) -- it is some other,
            // unrelated phi this cycle's own masking arithmetic happens
            // to route through, created by an *earlier*, unrelated step
            // for its own reasons (e.g. a divergent diamond's own
            // condition phi, which `peelConstantFlowPredecessors`
            // deliberately keeps as a genuine single-incoming phi, even
            // though it is trivially foldable in isolation, to sidestep
            // an unrelated `SSAUpdater` correctness issue -- see
            // `loop-uniform-check-fused-with-divergent-exit-inblock-use.ll`,
            // corrupted by an earlier, unscoped version of this fold).
            // Treating any such phi as an opaque closure boundary avoids
            // reaching into values that were never genuinely part of
            // this cycle's own mask-threading plumbing.
            !(isa<PHINode>(OpI) &&
              cast<PHINode>(OpI)->getNumIncomingValues() < 2) &&
            InScope.insert(OpI).second)
          Worklist.push_back(OpI);
  }
  // Roadmap L419(w): records, for every instruction this loop folds away,
  // exactly what it was folded to -- so that once the loop reaches a
  // fixed point, any of \p MaskPhis itself that got folded can still be
  // recovered (chasing one or more further replacements, since the value
  // an earlier fold produced may *itself* go on to be folded again before
  // the loop finishes) without ever dereferencing the now-freed
  // `PHINode`.
  DenseMap<Instruction *, Value *> Replacements;
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (Instruction *I : Worklist) {
      if (!InScope.contains(I))
        continue;
      Value *S = isa<PHINode>(I) ? cast<PHINode>(I)->hasConstantValue()
                                  : simplifyInstruction(I, SimplifyQuery(DL));
      if (!S || S == I)
        continue;
      I->replaceAllUsesWith(S);
      InScope.erase(I);
      Replacements[I] = S;
      I->eraseFromParent();
      Changed = true;
    }
  }
  SmallVector<Value *, 2> Result;
  for (PHINode *PN : MaskPhis) {
    Value *V = PN;
    // Roadmap L440: chase through `Replacements` using `V`'s raw pointer
    // *value* alone -- deliberately never `dyn_cast<Instruction>(V)` (the
    // form this replaced) first. Every instruction the fold loop above
    // erases is erased *immediately* after being recorded as a
    // `Replacements` key (`InScope.erase(I); Replacements[I] = S;
    // I->eraseFromParent();`), so by the time this loop runs, any `V`
    // that is itself a `Replacements` key is already a dangling pointer
    // to freed memory -- `dyn_cast`'s `getValueID()` read is a genuine,
    // reproduced (via `WaveReadLaneFirst.fp16.test`, nondeterministically
    // under ASLR) use-after-free the instant `V == PN` and `PN` itself
    // was folded away. `DenseMap<Instruction *, Value *>::find` only
    // ever hashes/compares the pointer *bit pattern* it is given, never
    // dereferencing it, so reinterpreting `V` as an `Instruction *`
    // purely for that lookup is safe regardless of whether the memory it
    // points to is still live: a "hit" can only occur when `V`'s bit
    // pattern exactly matches one of the (already-erased, but validly
    // recorded at insertion time) keys, which is exactly as precise as
    // the `dyn_cast` guard it replaces, without ever reading through a
    // potentially-freed pointer to get there.
    while (true) {
      auto It = Replacements.find(reinterpret_cast<Instruction *>(V));
      if (It == Replacements.end())
        break;
      V = It->second;
    }
    Result.push_back(V);
  }
  return Result;
}

/// (Roadmap H167) Whether \p CI is a call already carrying a per-lane
/// "read the real value only where active" mask from an *earlier* pass --
/// `feme::cpu::ResourceLoweringPass`'s `feme.cpu.resource.load.{typed,raw}`
/// or `feme::cpu::SPIRVResourceLoweringPass`'s `feme.cpu.image.load.*` --
/// whose \p Mask operand is not a compile-time-known constant. Unlike the
/// plain `load`s `applyStageMasks`'s own `MaskedLoads` tracks (see
/// `dependsOnTaintedValue`'s doc), these calls are already in their masked
/// form by the time `DiamondFlattener` ever runs: `UniformityInfo` is
/// computed once, before this pass does anything, against a module where
/// the call already exists exactly like this, and its own generic
/// operand-driven uniformity rule sees a non-constant mask operand and
/// (correctly, for that isolated call) may still call the *result*
/// uniform if every other operand is uniform -- even though a masked-off
/// lane really does read back an unrelated passthru value, exactly the
/// same per-lane-varying-result concern `applyStageMasks`'s own masked
/// loads have. A store/atomic kind is excluded: its own result (the
/// pre-op memory value, for an atomic) already has its own, separate
/// uniformity story (see roadmap H146/H166), and a store has no result at
/// all. Found reducing `InterlockedExchange.resources.32.test`'s own
/// post-loop, single-invocation (`if (GTID.x == 0)`) verification reads,
/// each guarded by a mask derived from that same invocation-index check,
/// where `feme::cpu::SIMDizePass`'s own fresh `UniformityInfo` (computed
/// with no visibility into this reasoning) went on to misclassify a
/// uniform branch built from one as divergent.
static bool isMaskDependentLoadResult(const CallInst *CI) {
  if (std::optional<MatchedMaskedMemOp> M = matchMaskedLoad(*CI))
    return !isKnownConstantMask(M->Mask);
  if (std::optional<MatchedResourceCall> M = matchResourceCall(*CI))
    return isLoad(M->Kind) && M->Mask && !isKnownConstantMask(M->Mask);
  if (std::optional<MatchedImageCall> M = matchImageCall(*CI))
    return M->Mask && !M->Texel && !M->AtomicValue &&
           !isKnownConstantMask(M->Mask);
  return false;
}

/// (Roadmap H75) Whether \p V is, or transitively depends (through any
/// chain of instruction operands, including `phi` incoming values) on, one
/// of the masked-load results \p Tainted collects -- see `applyStageMasks`'s
/// own `MaskedLoads` parameter comment for why this matters:
/// `UniformityInfo`, computed once before any masking happens, cannot see
/// that a masked load's result is now per-lane-varying (a masked-off lane
/// reads the passthru, not the real value), so a later branch's condition
/// built from it can be misclassified uniform. Also treats any call
/// `isMaskDependentLoadResult` recognizes as tainted directly, on the fly
/// (roadmap H167), for the identical reason -- these calls are already in
/// their masked form before this pass ever runs, so there is no
/// `applyStageMasks` rewrite to record them into \p Tainted the way a
/// plain `load` gets recorded. Bounded by \p Visited so a cyclic def-use
/// chain (a `phi` reachable from itself) terminates rather than looping
/// forever; deliberately conservative (a `false` positive here only costs
/// `DiamondFlattener::flatten` an extra `select` instead of a real
/// branch+`phi`, never unsound -- see `flatten`'s own comment where this
/// is used).
static bool dependsOnTaintedValue(Value *V,
                                  const SmallPtrSetImpl<Value *> &Tainted,
                                  SmallPtrSetImpl<Value *> &Visited) {
  if (Tainted.contains(V))
    return true;
  auto *I = dyn_cast<Instruction>(V);
  if (!I || !Visited.insert(V).second)
    return false;
  if (auto *CI = dyn_cast<CallInst>(I))
    if (isMaskDependentLoadResult(CI))
      return true;
  for (Value *Op : I->operands())
    if (dependsOnTaintedValue(Op, Tainted, Visited))
      return true;
  return false;
}


/// (Roadmap L116(a)) Recursively decomposes a masked load of a possibly
/// struct/array-typed \p Ty at \p Ptr into one masked load per leaf
/// (scalar or fixed-vector) type, reassembled with `insertvalue` --
/// mirroring `feme::cpu::SIMDizePass`'s own per-leaf `insertvalue`/
/// `extractvalue` decomposition of a divergent vector producer (roadmap
/// L21), but at this pass's own pre-`SIMDizePass`, per-scalar-lane level:
/// `MaskIntrinsics.cpp`'s `appendScalarMangling` has no notion of a
/// struct/array element type at all (masked load/store semantics are
/// inherently per-element, so there is no single-instruction lowering for
/// a masked op over a whole aggregate the way there is for a masked
/// scalar/vector), so a masked access to an aggregate-typed graphicsfuzz
/// local (e.g. a `struct`- or `mat4`-typed array element indexed by a
/// divergent index inside a loop) must be split into one real masked
/// load per leaf before it ever reaches that mangling. \p BaseAlign and
/// \p Offset track the *whole* access's own base alignment and this
/// leaf's byte offset within it, so each leaf gets its own correctly
/// narrowed alignment (see `SIMDize.cpp`'s own `commonAlignment` use for
/// the same reasoning at the wave-widened level). Returns nullptr, without
/// creating any further calls, the first time a leaf type
/// `appendScalarMangling` cannot mangle is reached (already reported
/// through the module's `LLVMContext` by that point).
static Value *createMaskedLoadRecursive(IRBuilderBase &Builder, Value *Ptr,
                                        Type *Ty, Align BaseAlign,
                                        uint64_t Offset, Value *Mask,
                                        const Twine &Name,
                                        SmallPtrSetImpl<Value *> *MaskedLoads) {
  const DataLayout &DL =
      Builder.GetInsertBlock()->getModule()->getDataLayout();
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(StructTy);
    Value *Agg = PoisonValue::get(StructTy);
    for (unsigned Idx = 0, End = StructTy->getNumElements(); Idx != End;
        ++Idx) {
      Value *FieldPtr = Builder.CreateStructGEP(
          StructTy, Ptr, Idx, Name + ".field" + Twine(Idx) + ".ptr");
      Value *FieldVal = createMaskedLoadRecursive(
          Builder, FieldPtr, StructTy->getElementType(Idx), BaseAlign,
          Offset + SL->getElementOffset(Idx), Mask,
          Name + ".field" + Twine(Idx), MaskedLoads);
      if (!FieldVal)
        return nullptr;
      Agg = Builder.CreateInsertValue(Agg, FieldVal, Idx);
    }
    return Agg;
  }
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    Type *ElemTy = ArrTy->getElementType();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    Value *Agg = PoisonValue::get(ArrTy);
    for (unsigned Idx = 0, End = ArrTy->getNumElements(); Idx != End; ++Idx) {
      Value *ElemPtr = Builder.CreateGEP(
          ArrTy, Ptr, {Builder.getInt32(0), Builder.getInt32(Idx)},
          Name + ".elt" + Twine(Idx) + ".ptr");
      Value *ElemVal = createMaskedLoadRecursive(
          Builder, ElemPtr, ElemTy, BaseAlign, Offset + Idx * ElemSize, Mask,
          Name + ".elt" + Twine(Idx), MaskedLoads);
      if (!ElemVal)
        return nullptr;
      Agg = Builder.CreateInsertValue(Agg, ElemVal, Idx);
    }
    return Agg;
  }
  // Leaf: a scalar or fixed-vector type, exactly what
  // `feme::cpu::createMaskedLoad` already supports directly.
  Value *Passthru = Constant::getNullValue(Ty);
  CallInst *Masked =
      feme::cpu::createMaskedLoad(Builder, Ptr, commonAlignment(BaseAlign, Offset).value(),
                                  Mask, Passthru, Name);
  if (!Masked)
    return nullptr;
  if (MaskedLoads)
    MaskedLoads->insert(Masked);
  return Masked;
}

/// (Roadmap L116(a)) The `store` counterpart of
/// `createMaskedLoadRecursive`: recursively decomposes a masked store of
/// a possibly struct/array-typed \p Val at \p Ptr into one masked store
/// per leaf, extracting each leaf's own value with `extractvalue` first.
/// Returns false, without creating any further calls, the first time an
/// unsupported leaf type is reached.
static bool createMaskedStoreRecursive(IRBuilderBase &Builder, Value *Val,
                                       Value *Ptr, Type *Ty, Align BaseAlign,
                                       uint64_t Offset, Value *Mask,
                                       const Twine &Name) {
  const DataLayout &DL =
      Builder.GetInsertBlock()->getModule()->getDataLayout();
  if (auto *StructTy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(StructTy);
    for (unsigned Idx = 0, End = StructTy->getNumElements(); Idx != End;
        ++Idx) {
      Value *FieldVal = Builder.CreateExtractValue(Val, Idx);
      Value *FieldPtr = Builder.CreateStructGEP(
          StructTy, Ptr, Idx, Name + ".field" + Twine(Idx) + ".ptr");
      if (!createMaskedStoreRecursive(
              Builder, FieldVal, FieldPtr, StructTy->getElementType(Idx),
              BaseAlign, Offset + SL->getElementOffset(Idx), Mask,
              Name + ".field" + Twine(Idx)))
        return false;
    }
    return true;
  }
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty)) {
    Type *ElemTy = ArrTy->getElementType();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
    for (unsigned Idx = 0, End = ArrTy->getNumElements(); Idx != End; ++Idx) {
      Value *ElemVal = Builder.CreateExtractValue(Val, Idx);
      Value *ElemPtr = Builder.CreateGEP(
          ArrTy, Ptr, {Builder.getInt32(0), Builder.getInt32(Idx)},
          Name + ".elt" + Twine(Idx) + ".ptr");
      if (!createMaskedStoreRecursive(Builder, ElemVal, ElemPtr, ElemTy,
                                      BaseAlign, Offset + Idx * ElemSize,
                                      Mask, Name + ".elt" + Twine(Idx)))
        return false;
    }
    return true;
  }
  // Leaf: a scalar or fixed-vector type, exactly what
  // `feme::cpu::createMaskedStore` already supports directly.
  CallInst *Masked = feme::cpu::createMaskedStore(
      Builder, Val, Ptr, commonAlignment(BaseAlign, Offset).value(), Mask);
  return Masked != nullptr;
}

/// Shared between `DiamondFlattener` (a divergent arm's masks) and
/// `LoopLinearizer` (a loop iteration's "active" masks) below. A given
/// memory access is left unmasked exactly when the mask that would govern
/// it is still the all-active constant at that point in \p BB -- checked
/// per access rather than once for the whole block, since a
/// `feme.stage.discard`/`.demote` call earlier in the same block can turn
/// an initially-constant mask into a real value partway through it.
///
/// (Roadmap H75) When \p MaskedLoads is non-null, every `feme.cpu.masked.
/// load` call this rewrites a plain `load` into is recorded there: unlike
/// every other masked form this function creates, a masked load's result
/// can itself feed a *later* branch's condition (e.g. a shared-memory
/// counter a single invocation reads back after a barrier, then compares
/// against an expected value), and that result is only per-lane-varying
/// *because* of the masking this function just performed -- a masked-off
/// lane gets the passthru (zero) instead of whatever the (possibly
/// perfectly uniform) memory location actually holds. `UniformityInfo` is
/// computed once, before this function ever runs (see
/// `feme::cpu::LinearizePass::run`), so it has no way to know that; only
/// `DiamondFlattener::flatten`, which calls this function block by block as
/// it walks, can. See `dependsOnTaintedValue` and its use in `flatten`'s own
/// divergent-vs-uniform branch classification.
void applyStageMasks(BasicBlock::iterator Begin, BasicBlock::iterator End,
                     MaskPair &Masks,
                     SmallPtrSetImpl<Value *> *MaskedLoads = nullptr) {
  for (Instruction &I : make_early_inc_range(make_range(Begin, End))) {
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
        case feme::StageOpKind::TaskPayloadAtomicRMW: {
          // (Roadmap L275) Mirrors `TaskPayloadStore` above exactly
          // (`offset`, operand 0, passed through unchanged regardless of
          // whether it is a compile-time constant or a genuinely dynamic
          // per-invocation `Value` -- `getOrInsertMaskedTaskPayloadAtomicRMW`
          // mangles its own callee purely off `Offset`'s actual type), but
          // unlike `TaskPayloadStore` this has a real result (the value
          // already stored at `offset` before the RMW applied), so the
          // masked call's own result replaces the original call's uses
          // rather than the call simply being erased outright.
          CallInst *Masked = createMaskedTaskPayloadAtomicRMW(
              B, Call->getArgOperand(0), Call->getArgOperand(1),
              Call->getArgOperand(2), Masks.SideEffect);
          Call->replaceAllUsesWith(Masked);
          Call->eraseFromParent();
          continue;
        }
        case feme::StageOpKind::TaskPayloadAtomicCmpXchg: {
          // (Roadmap L275) The compare-and-swap counterpart of
          // `TaskPayloadAtomicRMW` immediately above, for the identical
          // reason.
          CallInst *Masked = createMaskedTaskPayloadAtomicCmpXchg(
              B, Call->getArgOperand(0), Call->getArgOperand(1),
              Call->getArgOperand(2), Masks.SideEffect);
          Call->replaceAllUsesWith(Masked);
          Call->eraseFromParent();
          continue;
        }
        default:
          break; // Not a mask-affecting stage op; fall through below.
        }
      }
      if (std::optional<MatchedResourceCall> Matched =
              matchResourceCall(*Call)) {
        Value *Mask = isLoad(Matched->Kind) ? Masks.Live : Masks.SideEffect;
        narrowCallMask(Call, Call->arg_size() - 1, Mask);
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
      //
      // Roadmap L267: `QueryLevels`/`QuerySamples` (`createQueryLevels`/
      // `createQuerySamples`) are the sole `feme.cpu.image.*` kinds with
      // no trailing mask operand at all (see `MatchedImageCall::Mask`'s
      // own doc) -- `Call->arg_size() - 1` is their own last *real*
      // operand (`ImageIndex`), not a mask to narrow, so overwriting it
      // here silently corrupted the descriptor index into a divergent-
      // region predicate value (`%live.tN`, an `i1`), producing an
      // ill-typed call `SIMDize.cpp`'s widener (or, for a wholly-uniform
      // invocation, the JIT's own verifier) rejected outright -- a real
      // `dEQP-VK.glsl.texture_functions.query.texturequerylevels.*`/
      // `texturesamples.*` repro. `Matched->Mask` itself (null for these
      // two kinds) is the authoritative signal for whether a real mask
      // operand exists to narrow.
      if (std::optional<MatchedImageCall> Matched = matchImageCall(*Call)) {
        if (Matched->Mask) {
          Value *Mask = (Matched->Texel || Matched->AtomicValue)
                            ? Masks.SideEffect
                            : Masks.Live;
          narrowCallMask(Call, Call->arg_size() - 1, Mask);
        }
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
        // (roadmap H124h) `WaveActiveSum`/`Max`/.../`WavePrefixSum`/
        // `Product`'s own value operand (operand 0) needs exactly the same
        // "narrow to invocations still active *here*" treatment `Ballot`'s
        // predicate operand gets immediately above -- but unlike a
        // predicate, a reduce/scan operand cannot simply be ANDed with
        // `Masks.Live` (it is not always boolean, and even when it is,
        // `false` is not every one of these eleven kinds' own identity --
        // `ActiveBitAnd`'s is all-ones, `ActiveMin`'s is the type's own
        // max, etc.). Substituting `feme::cpu::getReduceIdentity`'s own
        // identity element for a masked-off lane instead (matching "Phase
        // 5"'s own `llvm.vector.reduce.* over select(M, X, identity)`
        // row -- `feme::cpu::WaveLoweringPass::lowerActiveReduce`/
        // `lowerPrefixReduce` already select this same way, just over
        // `FunctionWidener::widenWaveCall`'s own `Env.EntryMask`, which
        // only ever says "is this invocation real", never "is it still
        // inside this divergent region") keeps the reduction/scan
        // associative-identity-correct for every kind. Missing this let a
        // `WaveActiveSum`/etc. inside a divergent arm (e.g. `tid.x <= 1 ?
        // WaveActiveSum(v) : 0`) sum over the *whole* wave instead of just
        // the lanes that actually took that arm -- found reducing the
        // `WaveActiveSum.int32.test`/`WaveActiveMax.fp32.test`/... family
        // of CTS failures down to this exact shape.
        if (std::optional<WaveCallKind> Kind = classifyWaveCall(ID);
            Kind && isArithmeticReduceOrPrefixKind(*Kind) &&
            !isKnownConstantMask(Masks.Live)) {
          IRBuilder<> B(Call);
          Value *Operand = Call->getArgOperand(0);
          Type *OperandTy = Operand->getType();
          // (roadmap H124a) A `bvec2`-`bvec4`-shaped `WaveActiveSum`/
          // `Max`/... arrives here with a genuine vector-typed operand
          // (every `llvm.spv.wave.reduce.*`/`.product` intrinsic is
          // `llvm_any_ty`-overloaded) -- splat the scalar identity
          // `getReduceIdentity` returns across that same vector shape
          // rather than the (illegal) bare scalar `select` a vector
          // condition needs its operands to match.
          Type *EltTy = OperandTy->isVectorTy()
                            ? cast<VectorType>(OperandTy)->getElementType()
                            : OperandTy;
          Constant *Identity = getReduceIdentity(*Kind, EltTy);
          if (OperandTy->isVectorTy())
            Identity = ConstantVector::getSplat(
                cast<VectorType>(OperandTy)->getElementCount(), Identity);
          Call->setArgOperand(0, B.CreateSelect(Masks.Live, Operand, Identity,
                                                "wave.reduce.masked"));
        }
        // (roadmap H149) `WaveIsFirstLane`/`subgroupElect` needs exactly
        // the same "narrow to invocations still active *here*" treatment
        // `Ballot`'s predicate and a reduce's value operand get above --
        // but unlike either of those, `int_dx_wave_is_first_lane`/
        // `int_spv_wave_is_first_lane` are fixed-arity, zero-operand
        // intrinsics: there is no existing operand on the call itself to
        // narrow. Missing this let a `WaveIsFirstLane()` inside a
        // divergent arm (e.g. a `switch`'s `default` clause only one lane
        // actually reaches) report "first" relative to the *whole* wave's
        // original entry mask instead of just the invocations that
        // reached this specific arm, so a lone active lane there was
        // never recognized as first -- found reducing
        // `Feature/WaveOps/WaveIsFirstLane.test`'s own silent-wrong-data
        // failure down to this exact shape. Since the intrinsic's own
        // arity cannot grow, attach `Masks.Live` as a
        // `"feme.divergence.mask"` operand bundle instead -- a genuine
        // SSA use, so it is remapped/widened later exactly like any other
        // operand -- for `FunctionWidener::widenWaveCall` to AND into
        // `Env.EntryMask` once widened.
        if ((ID == Intrinsic::dx_wave_is_first_lane ||
             ID == Intrinsic::spv_wave_is_first_lane) &&
            !isKnownConstantMask(Masks.Live)) {
          OperandBundleDef DivergenceMask("feme.divergence.mask",
                                          ArrayRef<Value *>(Masks.Live));
          CallInst *NewCall = CallInst::Create(
              Call->getFunctionType(), Call->getCalledOperand(),
              ArrayRef<Value *>{}, ArrayRef<OperandBundleDef>(DivergenceMask),
              "", Call->getIterator());
          NewCall->setCallingConv(Call->getCallingConv());
          NewCall->setAttributes(Call->getAttributes());
          NewCall->takeName(Call);
          Call->replaceAllUsesWith(NewCall);
          Call->eraseFromParent();
        }
        // (roadmap L152) `subgroupBroadcast`/`OpGroupNonUniformBroadcast`
        // needs this exact same "narrow to invocations still active
        // *here*" treatment `WaveIsFirstLane` gets immediately above, for
        // an analogous reason: unlike `Ballot`'s predicate or a reduce's
        // value operand, `Broadcast`'s own `Id` operand identifies the
        // *source lane*, not "is the current lane active", so it cannot
        // narrow `feme::cpu::FunctionWidener::widenWaveCall`'s own
        // `WideMask` the way those operands do -- that function seeds
        // every wave call's `WideMask` from the wave's whole, original
        // `Env.EntryMask`, narrowing it further only when a call's own
        // operand does, or (for `IsFirstLane`, and now `Broadcast` too)
        // when this bundle does. Missing this let `feme::cpu::
        // WaveLoweringPass::lowerBroadcast`'s "any lane `WideMask` marks
        // active will do" logic (see that function's own comment) pick a
        // lane active per the *whole wave*'s original entry mask that was
        // not actually one of the invocations calling this particular
        // `Broadcast` at all, whenever it sat inside a divergent region
        // narrower than the whole wave -- found reducing `dEQP-VK.
        // subgroups.ballot_broadcast.compute.subgroupbroadcast_nonconst_*`
        // (its own "lane id that is only uniform across active lanes"
        // case) down to this exact shape. Unlike `IsFirstLane`,
        // `Broadcast` is not a fixed-arity, zero-operand intrinsic, so its
        // existing operands are preserved on the replacement call
        // alongside the new bundle rather than dropped.
        if (ID == Intrinsic::spv_wave_broadcast &&
            !isKnownConstantMask(Masks.Live)) {
          OperandBundleDef DivergenceMask("feme.divergence.mask",
                                          ArrayRef<Value *>(Masks.Live));
          CallInst *NewCall = CallInst::Create(
              Call->getFunctionType(), Call->getCalledOperand(),
              {Call->getArgOperand(0), Call->getArgOperand(1)},
              ArrayRef<OperandBundleDef>(DivergenceMask), "",
              Call->getIterator());
          NewCall->setCallingConv(Call->getCallingConv());
          NewCall->setAttributes(Call->getAttributes());
          NewCall->takeName(Call);
          Call->replaceAllUsesWith(NewCall);
          Call->eraseFromParent();
        }
      }
      continue;
    }
    if (auto *RI = dyn_cast<ReturnInst>(&I)) {
      if (feme::getShaderStage(*RI->getFunction()) ==
          feme::ShaderStage::Fragment) {
        // Roadmap L292: `BB` can legitimately have this function called on
        // it more than once -- e.g. once by `DiamondFlattener::run()`'s own
        // earlier, whole-function walk (necessarily seeded with a guess,
        // since it runs before `LoopLinearizer` ever narrows a loop's own
        // exit-time mask -- see `dEQP-VK.glsl.demote.dynamic_loop_always`'s
        // own `linearizeCycle` comment on `ExitBlock`), and again later with
        // the real, fully-updated `Masks` once that loop is linearized.
        // Erase any such stale call already sitting here first, so this
        // block never ends up with two -- only the final, most accurate one
        // should ever remain.
        for (Instruction &Existing : make_early_inc_range(*RI->getParent()))
          if (auto *ExistingCall = dyn_cast<CallInst>(&Existing);
              ExistingCall && feme::cpu::isReturnMasksCall(*ExistingCall))
            ExistingCall->eraseFromParent();
        IRBuilder<> B(RI);
        createReturnMasks(B, Masks.Live, Masks.SideEffect);
      }
      continue;
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!LI->isSimple() || isKnownConstantMask(Masks.Live))
        continue; // Atomic/volatile: not this milestone's problem yet.
      IRBuilder<> B(LI);
      // (Roadmap L116(a)) A struct/array-typed access is decomposed into
      // one masked load per leaf, reassembled with `insertvalue`, rather
      // than handed to `createMaskedLoad` directly -- see
      // `createMaskedLoadRecursive`'s own comment for why. A scalar or
      // fixed-vector-typed access (the overwhelmingly common case) still
      // goes through the single, non-recursive call below unchanged.
      if (isa<StructType>(LI->getType()) || isa<ArrayType>(LI->getType())) {
        Value *Result = createMaskedLoadRecursive(
            B, LI->getPointerOperand(), LI->getType(), LI->getAlign(),
            /*Offset=*/0, Masks.Live, LI->getName(), MaskedLoads);
        if (!Result) // See the scalar/vector case's own comment below.
          continue;
        LI->replaceAllUsesWith(Result);
        LI->eraseFromParent();
        continue;
      }
      Value *Passthru = Constant::getNullValue(LI->getType());
      CallInst *Masked =
          createMaskedLoad(B, LI->getPointerOperand(), LI->getAlign().value(),
                           Masks.Live, Passthru, LI->getName());
      // A null result means `LI`'s type is a shape `MaskIntrinsics.cpp`'s
      // `appendScalarMangling` cannot yet mangle (an unsupported scalar
      // element type inside an otherwise-supported vector, most notably;
      // a struct/array itself is now handled above), which has already
      // reported an error through `LI`'s own `LLVMContext` (caught by
      // `feme::cpu::runPipeline`'s `ErrorDiagnosticGuard`). Leave `LI`
      // itself unmasked and unmodified rather than RAUW/erase with a call
      // that was never created, so this loop keeps making progress on the
      // rest of `BB` instead of crashing on a null `CallInst *`.
      if (!Masked)
        continue;
      LI->replaceAllUsesWith(Masked);
      LI->eraseFromParent();
      if (MaskedLoads)
        MaskedLoads->insert(Masked);
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (!SI->isSimple() || isKnownConstantMask(Masks.SideEffect))
        continue;
      IRBuilder<> B(SI);
      Type *ValTy = SI->getValueOperand()->getType();
      // (Roadmap L116(a)) See the load case's own comment above:
      // struct/array-typed stores are decomposed per leaf instead of
      // handed to `createMaskedStore` directly.
      if (isa<StructType>(ValTy) || isa<ArrayType>(ValTy)) {
        if (!createMaskedStoreRecursive(
                B, SI->getValueOperand(), SI->getPointerOperand(), ValTy,
                SI->getAlign(), /*Offset=*/0, Masks.SideEffect, SI->getName()))
          continue; // See the load case's own comment.
        SI->eraseFromParent();
        continue;
      }
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

/// Convenience overload for the common case of narrowing a whole block at
/// once (every caller except `LoopLinearizer::linearizeCycle`'s own
/// `CheckBlock` handling, which -- roadmap L442 -- needs to narrow two
/// sub-ranges of one block with two different masks; see that call site's
/// own comment for why).
void applyStageMasks(BasicBlock &BB, MaskPair &Masks,
                     SmallPtrSetImpl<Value *> *MaskedLoads = nullptr) {
  applyStageMasks(BB.begin(), BB.end(), Masks, MaskedLoads);
}

/// (Roadmap H125) After if-converting a loop with a divergent exit, an
/// ordinary loop-carried value (any \p Header `phi` besides the two mask
/// phis \p LoopLinearizer::makeActivePNPair itself just created --
/// \p MaskPhis -- which `addLatchIncoming` already merges correctly) that
/// is still read *after* the loop needs its own value frozen for a lane
/// that was not really part of this "wide" iteration, exactly the way
/// `applyStageMasks` already narrows a memory access or a wave reduce's
/// operand above. Without this, a lane whose own real per-lane trip count
/// has already been exhausted keeps having its carried value overwritten
/// by the body's result on every subsequent wide iteration, since the
/// if-converted body always executes unconditionally for the whole wave
/// (see the file comment's "Loops with a divergent exit" discussion) --
/// found reducing `WaveActiveBitXor.convergence.test`'s own
/// divergent-trip-count loop down to this exact shape, but the gap is
/// general: it affects any loop-carried value read after the loop exits,
/// not just a wave reduce's result (confirmed with a second, wave-op-free
/// repro).
///
/// Deliberately scoped to only a phi with a use \p InLoop reports as
/// outside the loop: a value only ever consumed *inside* the loop body is
/// already correctly narrowed at each such point of use by
/// `applyStageMasks` itself (a masked load/store/resource call/wave
/// reduce operand there already uses whatever the *current* mask is,
/// which is exactly as correct as this phi's own "current" value would be
/// -- freezing it too would be redundant), and, more importantly, can
/// itself be one of this pass's own genuinely-uniform "whole loop" values
/// (like a trip-count induction variable a separate, uniform latch check
/// consumes -- see `simdize-loop.ll`'s own `%i`/`loop.cond`, never read
/// after the loop): unconditionally freezing that kind of phi would
/// (correctly, but pointlessly, since nothing outside the loop ever
/// observes it) turn it genuinely divergent, corrupting `UniformityInfo`'s
/// otherwise-still-accurate uniform classification of whatever *it* feeds
/// downstream -- exactly the shape that broke `simdize-loop.ll` (and its
/// two siblings) during this milestone's own development, once this
/// function stopped scoping itself this way.
///
/// \p BodyMask is the mask that was in effect while \p Latch's own
/// predecessor chain (the loop body) computed each such phi's incoming
/// value -- i.e., whether this specific wide iteration was a real one for
/// a given lane.
///
/// Roadmap L419(ac): \p RelayCaptured additionally force-includes any phi
/// it lists as "used outside the loop" even when `InLoop` alone would
/// disagree -- needed for a `Header` phi whose only real external use is
/// being captured, via a relay-chain hop's own phi, by
/// `freezeRelayChainExitValues` elsewhere (see that function's own
/// comment for the full root-cause writeup): that hop lives *inside* the
/// cycle (`InLoop` correctly says so), so this phi's literal direct users
/// are indeed all in-loop, yet the value still needs the exact same
/// sticky, self-referential freeze this function already applies to any
/// other externally-read phi, since a relay-chain hop's own phi silently
/// re-reads whatever `PN` currently holds on every subsequent iteration
/// once that hop's own distinguishing predecessor edge is gone.
static void
freezeLoopCarriedValues(BasicBlock *Header, BasicBlock *Latch, Value *BodyMask,
                        ArrayRef<PHINode *> MaskPhis,
                        function_ref<bool(const BasicBlock *)> InLoop,
                        ArrayRef<PHINode *> RelayCaptured = {}) {
  if (isKnownConstantMask(BodyMask))
    return; // All-active: the unmasked value is already correct.
  IRBuilder<> B(Latch->getTerminator());
  for (PHINode &PN : Header->phis()) {
    if (llvm::is_contained(MaskPhis, &PN))
      continue;
    int Idx = PN.getBasicBlockIndex(Latch);
    if (Idx == -1)
      continue;
    Value *NewValue = PN.getIncomingValue(Idx);
    if (NewValue == &PN)
      continue; // Already trivially frozen; nothing this iteration changed.
    bool UsedOutsideLoop =
        llvm::is_contained(RelayCaptured, &PN) || any_of(PN.users(), [&](User *U) {
          return !InLoop(cast<Instruction>(U)->getParent());
        });
    if (!UsedOutsideLoop)
      continue; // Only ever read inside the loop: nothing to freeze.
    PN.setIncomingValue(
        Idx, B.CreateSelect(BodyMask, NewValue, &PN, PN.getName() + ".frozen"));
  }
}

/// Roadmap L419(ac): repairs every relay-chain hop's own phi (see
/// `ExitCheck::RelayChain`'s own comment) for the predecessor edge about
/// to be orphaned once the caller erases \p Entry's own `CondBr` to it,
/// *before* that happens -- called once per `HeaderExit`/`LatchExit`
/// relay match, right before its own `->Br->eraseFromParent()`.
///
/// Root cause (`loop_in_switch`/`elseblock`/`ifblock`'s own shared shape,
/// reduced from `dEQP-VK.glsl.switch.loop_in_switch`'s own
/// `while_loop_in_switch_dynamic_fragment`): `recoverRelayExitCheck`'s own
/// generalized chase (roadmap L419(ab)) now resolves `HeaderExit`/
/// `LatchExit` through one or more relay hops that themselves still own
/// real, multi-valued `PHINode`s distinguishing "arrived via this exit
/// edge" from "arrived via the loop's own continue edge" (StructurizeCFG
/// routinely shares a single dispatch block, like this repro's own
/// `Flow`, between a loop's real `break` and its real `continue`, picking
/// between them with exactly such a phi). Once the caller erases
/// `Header`'s (or `Latch`'s) own `CondBr` and replaces it with an
/// unconditional branch straight to `StayInLoop`, the first relay hop's
/// only predecessor (`Header`/`Latch` itself) stops reaching it,
/// orphaning every one of these phis' exit-edge contribution --
/// eventually (once `EliminateUnreachableBlocks` runs, much later, at the
/// very end of this whole pass) collapsing each such phi down to its
/// *other*, surviving incoming value alone, discarding the correct
/// already-exited value for every lane that logically took this exit
/// edge, and replacing it with whatever the loop's *own, still actively
/// re-executing* continue-edge computes instead -- silently corrupting
/// exactly those lanes' own final result (confirmed, via direct IR
/// inspection, to be the root cause of this shape's "Image mismatch" CTS
/// failures, not a masking omission anywhere else).
///
/// Fixed by walking the chain (in order) and, for each phi with an
/// incoming edge from the chain's previous hop (the soon-to-be-orphaned
/// one), `select`-wrapping every one of its *other* (surviving) incoming
/// values with \p GuardMask -- the same per-lane "is this iteration a
/// real one for this lane" mask `freezeLoopCarriedValues` itself already
/// uses for an ordinary `Header` phi, captured by the caller immediately
/// after its own `stayInLoop` call -- choosing the orphaned edge's own
/// value instead whenever a lane has already (this iteration or any
/// earlier one) taken the exit edge instead. Whenever the orphaned
/// value is itself one of \p Header's own loop-carried phis (this
/// repro's own shape: the exit edge's value is `Header`'s raw running
/// phi, unmodified), that phi is also recorded into \p RelayCaptured so
/// the caller can forward it to `freezeLoopCarriedValues`'s own
/// `RelayCaptured` parameter -- *that* phi itself needs the exact same
/// sticky, self-referential freeze applied to its own backedge too
/// (not just this function's one-shot `select` here), since otherwise
/// its own value keeps being overwritten by the loop's still-executing
/// body on every subsequent iteration, even once correctly selected here.
static void
freezeRelayChainExitValues(ArrayRef<BasicBlock *> Chain, BasicBlock *Entry,
                           Value *GuardMask, BasicBlock *Header,
                           SmallPtrSetImpl<PHINode *> &RelayCaptured) {
  BasicBlock *Prev = Entry;
  for (BasicBlock *RB : Chain) {
    IRBuilder<> B(RB->getTerminator());
    for (PHINode &PN : RB->phis()) {
      int OrphanIdx = PN.getBasicBlockIndex(Prev);
      if (OrphanIdx == -1)
        continue;
      Value *OrphanValue = PN.getIncomingValue(OrphanIdx);
      if (auto *OrphanPN = dyn_cast<PHINode>(OrphanValue);
          OrphanPN && OrphanPN->getParent() == Header)
        RelayCaptured.insert(OrphanPN);
      for (unsigned Idx = 0, E = PN.getNumIncomingValues(); Idx != E; ++Idx) {
        if (static_cast<int>(Idx) == OrphanIdx)
          continue;
        Value *Kept = PN.getIncomingValue(Idx);
        if (Kept == OrphanValue)
          continue; // Already identical; no divergence to preserve.
        PN.setIncomingValue(
            Idx, B.CreateSelect(GuardMask, Kept, OrphanValue,
                                PN.getName() + ".relay.frozen"));
      }
    }
    Prev = RB;
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
  /// Roadmap K6z: optional, only ever supplied by `LoopLinearizer::
  /// linearizeCycle`'s own call sites (never `DiamondFlattener::run`'s
  /// whole-function pass, nor `flattenLoopBodyDiamond`'s single-diamond
  /// callers) -- lets `validate`/`flatten` walk straight through a nested
  /// child cycle's header that `linearizeCyclePostOrder`'s post-order
  /// already fully linearized before this call, rather than always
  /// stopping at it (the pre-K6z behavior every other cycle-boundary
  /// shape still gets, see `CycleBoundaryBlocks`). \p GetAlreadyLinearized
  /// CycleExit(Header) returns the single safe exit block to continue the
  /// walk from if \p Header is such a cycle (its own already-finalized
  /// interior is never touched, matching `HeaderActiveMasks`'s "was this
  /// child linearized already" query one layer up), or `nullptr`
  /// otherwise (not such a cycle at all, or one with more than one exit
  /// block -- not yet supported here). Deliberately a callback instead of
  /// a raw `CycleInfo::getExitBlocks` call: see `isLoopControlEdge`'s own
  /// comment for the confirmed use-after-free/staleness crashes a live or
  /// freshly-cached call of that would reintroduce here; the caller
  /// already maintains its own mutation-safe `ExitBlocksByCycle` cache
  /// precisely to avoid that, so this threads *that* answer through
  /// instead of re-deriving an unsafe one.
  using GetAlreadyLinearizedCycleExitFn =
      llvm::function_ref<BasicBlock *(BasicBlock *)>;
  /// Roadmap K6z: paired with `GetAlreadyLinearizedCycleExit` above --
  /// called (only by `flatten`, never the read-only `validate`) on the
  /// already-linearized child cycle's own header the first time this
  /// walk reaches it, with the `MaskPair` this walk has accumulated at
  /// that point, so the child's own entry can be retroactively narrowed
  /// by it exactly the way `LoopLinearizer::rethreadNestedEntryMasks`
  /// narrows any other already-linearized child cycle reached via
  /// `applyStageMasks` -- see that method's own comment for why this
  /// narrowing matters at all (a lane the enclosing walk already masked
  /// off must not still enter the child "live"). A no-op default (never
  /// supplied as null) when \p GetAlreadyLinearizedCycleExit is also null.
  using NarrowChildEntryMasksFn =
      llvm::function_ref<void(BasicBlock *, const MaskPair &)>;

  /// Roadmap L447: true only for the single instance `run()`'s own
  /// whole-function-pass caller constructs (see that call site's own
  /// comment), never for an instance built for `flattenLoopBodyDiamond`'s
  /// or `flattenLoopBodyRegion`'s own `LoopLinearizer::linearizeCycle`
  /// call sites. Gates the "defer an in-cycle mask-op diamond entirely to
  /// `LoopLinearizer`" check in `validate`/`flatten` (see their own
  /// comments): that check is correct, and needed, only for `run()`'s own
  /// walk, which executes before `LoopLinearizer` even starts and so has
  /// no way yet to compute the diamond's real per-iteration mask -- but
  /// `flattenLoopBodyDiamond`/`flattenLoopBodyRegion` are themselves part
  /// of `LoopLinearizer`'s own masking machinery, called only after a
  /// cycle's real mask is known (or being computed); unconditionally
  /// applying the same defer inside *their* recursive `validate`/`flatten`
  /// calls would make them defer the very diamond they exist to flatten,
  /// leaving it permanently unflattened (confirmed live: `feme-cpu-
  /// simdize` then rejects the untouched divergent branch outright).
  DiamondFlattener(
      Function &F, DominatorTree &DT, PostDominatorTree &PDT, CycleInfo &CI,
      UniformityInfo &UI, SmallPtrSetImpl<BasicBlock *> &MergeVisited,
      SmallPtrSetImpl<BasicBlock *> &ResolvedBlocks,
      GetAlreadyLinearizedCycleExitFn GetAlreadyLinearizedCycleExit = nullptr,
      NarrowChildEntryMasksFn NarrowChildEntryMasks = nullptr,
      bool IsWholeFunctionPass = false)
      : F(F), DT(DT), PDT(PDT), CI(CI), UI(UI),
        GetAlreadyLinearizedCycleExit(GetAlreadyLinearizedCycleExit),
        NarrowChildEntryMasks(NarrowChildEntryMasks),
        MergeVisited(MergeVisited), ResolvedBlocks(ResolvedBlocks),
        IsWholeFunctionPass(IsWholeFunctionPass) {}

  /// Validates and then flattens every divergent diamond reachable from
  /// \p F's entry block, or from the exit block of any cycle reached along
  /// the way (see `validate`'s comment), without crossing into a cycle
  /// itself. Returns whether \p F was changed; a validation failure is
  /// diagnosed and leaves \p F untouched (returns false).
  bool run();

  /// Attempts to flatten a single divergent diamond -- \p Start's own
  /// two-way branch and its reconvergence point (computed via
  /// `immediatePostDom`) -- as an ordinary, unconditionally-live diamond
  /// (a constant-true `MaskPair`), exactly as `run()` would flatten any
  /// diamond outside a loop. Used by `LoopLinearizer` (roadmap H165) for a
  /// divergent branch found inside a loop body that turns out to have no
  /// bearing on the loop's own iteration decision: both its arms, and its
  /// reconvergence point, must stay strictly inside the loop body, never
  /// crossing one of the loop's own control edges (see
  /// `isLoopControlEdge`) -- checked by requiring `validate` to reach the
  /// reconvergence point cleanly, with `CycleBoundaryBlocks` left empty,
  /// rather than stopping early at such an edge the way it tolerates for
  /// its own, unrelated whole-function walk. Silently returns
  /// `std::nullopt`, emitting no diagnostic, when \p Start does not have
  /// this shape (e.g. an arm reaches the loop's own header, latch, or
  /// exit block, or the shape is otherwise unsupported) -- the caller is
  /// expected to fall back to its own, more specific diagnostic in that
  /// case. On success, returns the diamond's own (now-flattened)
  /// reconvergence block.
  std::optional<BasicBlock *> flattenLoopBodyDiamond(BasicBlock *Start);

  /// Roadmap L292: whether any block strictly between \p Start (inclusive)
  /// and \p End (exclusive) contains a `feme.stage.*` mask-affecting op
  /// (the same kinds `hasStageMaskOps` checks for, whole-function). Only
  /// safe to call once the caller has already confirmed, via `validate`,
  /// that the region is a bounded, acyclic chain of reconverging diamonds
  /// (no cycle boundary crossed) -- the plain successor-walk below assumes
  /// that, rather than re-deriving it itself.
  bool regionHasStageMaskOps(BasicBlock *Start, BasicBlock *End) {
    SmallPtrSet<BasicBlock *, 8> Visited;
    SmallVector<BasicBlock *, 8> Worklist{Start};
    while (!Worklist.empty()) {
      BasicBlock *BB = Worklist.pop_back_val();
      if (BB == End || !Visited.insert(BB).second)
        continue;
      for (Instruction &I : *BB) {
        auto *Call = dyn_cast<CallInst>(&I);
        if (!Call)
          continue;
        feme::StageOpKind Kind;
        if (feme::isStageOpCall(*Call, &Kind) &&
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
        // Roadmap L425(a) (2nd bug): mirrors `LoopLinearizer::
        // linearizeCycle`'s own identical `CycleHasMaskOps` check (see
        // its comment, roadmap K6s) -- a `feme.cpu.resource.*`/`feme.cpu.
        // image.*` call's own trailing mask operand starts out as the
        // compile-time constant `true`, relying entirely on this pass's
        // `applyStageMasks` to narrow it to the real per-iteration
        // predicate. Without this check, `flattenLoopBodyDiamond` would
        // "successfully" pre-flatten a loop-body diamond containing one
        // of these calls using its own always-seeded `AllActive` guess
        // (see its own comment) *before* `LoopLinearizer`'s real,
        // loop-carried mask is ever computed -- stamping the call's mask
        // operand with a value derived from that bogus guess. The call's
        // enclosing block, now a plain straight-line block (its
        // conditional branch already replaced with an unconditional one),
        // is then walked right past a second time by `LoopLinearizer`'s
        // own, correct `flattenLoopBodyRegion` pass, which blindly
        // re-stamps the same call's mask operand again with the *real*
        // per-iteration mask -- except `LoopLinearizer`'s own walk reaches
        // it with the loop's single, backedge-independent outer `active.
        // live`/`active.sideeffect` phi (not yet narrowed by this
        // specific inner diamond), clobbering the correct, narrower mask
        // the premature first pass had already (coincidentally, only for
        // that one visit) computed correctly. Found reducing
        // `AllMemoryBarrierWithGroupSync.test`'s `UavCounter[0]++` inside
        // `for (uint I = 0; I < 512; ++I) { if (GTID.x == I) { ... } }`
        // down to exactly this double-masking shape via targeted
        // `errs()` tracing of every `applyStageMasks` resource-call
        // rewrite.
        //
        // Deliberately narrower than `CycleHasMaskOps`'s own identical
        // check: a *pure read* (`StoredValue`/`Texel`/`AtomicValue` all
        // null -- `feme.cpu.resource.load.*`/`.getdimensions.*`,
        // `feme.cpu.image.load.*`/`.sample.*`/`.querylod.*`) has no
        // memory side effect for a wrong (too-early, always-active) mask
        // to corrupt -- only a write (a store or an atomic) does. Gating
        // on that distinction (rather than "any resource/image call at
        // all", as first tried) matters because `flattenLoopBodyDiamond`
        // pre-flattening a *read-only* diamond is not just harmless, it
        // is sometimes *required*: `LinearizesCheckBlockChainedDivergent
        // CheckBypassToStayInLoop`'s own `L377(a)` shape depends on a
        // read-only loop-body diamond (`feme.cpu.resource.getdimensions.
        // raw`/`.load.raw`) being pre-flattened here so its own divergent-
        // branch-bypass fold can run against an already-simplified body;
        // over-widening this check to every resource/image call regressed
        // exactly that case (a `PHINode` with a mismatched predecessor
        // count, caught by `verifyModule`) the first time this fix was
        // tried.
        if (std::optional<MatchedResourceCall> RC = matchResourceCall(*Call)) {
          if (RC->StoredValue)
            return true;
        } else if (std::optional<MatchedImageCall> IC =
                       matchImageCall(*Call)) {
          if (IC->Texel || IC->AtomicValue)
            return true;
        }
      }
      for (BasicBlock *Succ : successors(BB))
        Worklist.push_back(Succ);
    }
    return false;
  }


  /// Roadmap L292: like `flattenLoopBodyDiamond`, but for an entire,
  /// externally-identified loop-body region `[Start, End)` in one walk,
  /// threading an externally-supplied, already loop-carried \p Masks
  /// through it rather than always reseeding a fresh, unconditional
  /// "every lane active" pair the way `flattenLoopBodyDiamond`'s own
  /// `AllActive` seed does. That reseed is wrong for a `feme.stage.
  /// discard`/`.demote` whose effect must accumulate across a loop's own
  /// iterations (a lane masked off by an earlier iteration must stay
  /// masked off, not look "fresh" again the next time around) rather than
  /// reset every time -- see `LoopLinearizer::linearizeCycle`'s own
  /// fully-uniform-loop-with-body-mask-ops case, which this method
  /// exists for: a loop whose own header/latch exit check is itself
  /// uniform (so is left completely untouched, unlike the genuinely
  /// divergent-exit cases `linearizeCycle` already handles) can still
  /// have a `feme.stage.*` mask op somewhere in its straight-line body or
  /// inside a plain uniform internal diamond (see `dEQP-VK.glsl.demote.
  /// dynamic_loop_always`'s own `if (i > 0) demote`), and that still
  /// needs its own loop-carried mask threaded through exactly the same
  /// `applyStageMasks`/diamond-merge machinery as a genuinely divergent
  /// loop's body already gets. \p Start and \p End must both already be
  /// confirmed by the caller to be ordinary loop-body members (in
  /// particular, never this cycle's own header -- see
  /// `isLoopControlEdgeThroughRelay`);
  /// this still stops early (returning `std::nullopt`, emitting no
  /// diagnostic) if the walk from \p Start does not reach \p End cleanly
  /// without crossing some *other* cycle boundary along the way (e.g. a
  /// nested child cycle's own header, or this cycle's own latch/exit
  /// reached by some path other than directly to \p End) -- the caller
  /// is expected to diagnose that itself, the same as
  /// `flattenLoopBodyDiamond`'s own callers do.
  std::optional<MaskPair> flattenLoopBodyRegion(BasicBlock *Start,
                                                BasicBlock *End,
                                                MaskPair Masks);

  /// (Roadmap L419(w)) Flattens exactly \p Start's own two-way branch into
  /// masked, unconditional form, given \p Masks as the mask already in
  /// effect entering \p Start -- unlike `flattenLoopBodyDiamond`/
  /// `flattenLoopBodyRegion`, does *not* itself reclassify or validate
  /// `Start`'s own cycle-boundary status the way `validate`'s top-level
  /// walk always does (see its own `isLoopControlOrHeaderRelayEdge`
  /// check): the caller has already independently confirmed `Start`'s own
  /// branch genuinely needs flattening despite sitting at a cycle's own
  /// header -- exactly `LoopLinearizer::flattenTaintedBranchesInRegion`'s
  /// own use, for a `PreRegion`/`PostRegion` block whose condition only
  /// became mask-dependent *after* this very call's own `Masks` narrowed
  /// a resource/image load inside it (see that function's own comment for
  /// the full root-cause writeup) -- a shape `flattenLoopBodyRegion`
  /// itself cannot reach did it start exactly at `Start`, since `validate`
  /// would immediately (and, for every *other* caller, correctly) treat
  /// any cycle header's own branch as "`LoopLinearizer`'s problem, not an
  /// error" and refuse to flatten it. Each arm's own *interior* is still
  /// fully subject to the ordinary `validate`/`flatten` rules (an arm may
  /// itself contain a nested diamond, or reach a nested child cycle
  /// boundary) -- only this one, outermost step bypasses the check.
  /// Returns `std::nullopt`, emitting no diagnostic, when \p Start does
  /// not have the simple shape this requires (no two-way branch, no
  /// immediate post-dominator, an empty arm, a reconvergence point without
  /// exactly two predecessors, or either arm's own interior fails to
  /// validate) -- the caller is expected to diagnose that itself.
  std::optional<BasicBlock *> flattenKnownDivergentBranch(BasicBlock *Start,
                                                           MaskPair Masks);
  /// Roadmap L292: a read-only, structure-only pre-check for whether a
  /// later `flattenLoopBodyRegion(Start, End, ...)` call will succeed --
  /// mirrors that method's own `validate` precondition exactly, without
  /// requiring (or mutating) any real `MaskPair` at all. A caller that
  /// needs to mutate something else first (e.g. creating this cycle's own
  /// loop-carried mask phis at `Start`'s own `Header`) when it already
  /// knows it must then call `flattenLoopBodyRegion` -- but wants to avoid
  /// leaving that mutation stranded, with no corresponding backedge value
  /// ever added, if the region turns out unsupported -- should call this
  /// first and diagnose-and-return before mutating anything, exactly as
  /// `LoopLinearizer::linearizeCycle`'s own "bug 3" branch does (confirmed
  /// via a real, reproduced `verifyModule` failure -- a `phi` with only
  /// one incoming value for its parent's two predecessors -- once this
  /// check was added to let a previously-untested failure path through
  /// that branch actually get exercised).
  bool canFlattenLoopBodyRegion(BasicBlock *Start, BasicBlock *End) {
    CycleBoundaryBlocks.clear();
    return validate(Start, End, /*Quiet=*/true) && CycleBoundaryBlocks.empty();
  }

  /// Roadmap L286: every nested cycle's own header this pass's `flatten`
  /// walk stopped at (see `CycleBoundaryBlocks`/the identical early-return
  /// in `flatten` itself), mapped to the `MaskPair` that walk had
  /// accumulated at the point it reached that header -- i.e. "should this
  /// lane actually be considered live entering this not-yet-linearized
  /// cycle at all", reflecting whatever enclosing divergent diamond(s)
  /// this cycle's own preheader edge sits inside. `LoopLinearizer` (a
  /// wholly separate pass, run afterward against the same, by-then-
  /// unchanged IR -- see `LinearizePass::run`) has no other way to learn
  /// this: its own `makeActivePNPair` otherwise always seeds a cycle's
  /// entry mask as a bare, unconditional `true` on every non-backedge
  /// predecessor, correct only when nothing upstream of the cycle ever
  /// narrowed which lanes should even reach it. When an enclosing
  /// diamond's arm contains the entire cycle (exactly the shape
  /// `CycleBoundaryBlocks` exists to tolerate, e.g. `dEQP-VK.glsl.loops.
  /// special.for_dynamic_iterations.dowhile_trap_fragment`'s own outer
  /// `for` loop, whose divergent trip-count check gates whether its
  /// nested `do`-`while` body should run at all for a given lane), that
  /// default silently drops the outer diamond's own narrowing on the
  /// floor: a lane the outer check already decided should skip the body
  /// entirely still enters the nested cycle "live", and if that cycle's
  /// own exit condition happens to never independently become false for
  /// such a lane (as `dowhile_trap`'s does not -- its own check shares the
  /// outer loop's induction variable, frozen forever once that lane can
  /// never make outer-loop progress), the whole wave's `mask.any`
  /// reduction spins forever. See `LoopLinearizer`'s own
  /// `DiamondFlattenedEntryMasks` member for the consuming side.
  const DenseMap<BasicBlock *, WeakMaskPair> &getCycleBoundaryMasks() const {
    return CycleBoundaryMasks;
  }

private:
  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  CycleInfo &CI;
  UniformityInfo &UI;

  /// Roadmap K6z: see the constructor's own parameter comment. Null for
  /// every `DiamondFlattener` except the two `LoopLinearizer::
  /// linearizeCycle` constructs for its own "fully uniform loop with body
  /// mask ops" case.
  GetAlreadyLinearizedCycleExitFn GetAlreadyLinearizedCycleExit;
  NarrowChildEntryMasksFn NarrowChildEntryMasks;

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
  ///
  /// Roadmap L197: deliberately does *not* call `CI.getExitBlocks` (live or
  /// via any precomputed cache of it) at all. \p Cur is already known (by
  /// every caller) to be a member of its own cycle \p C; since \p Cur has a
  /// direct edge to \p Target, \p Target is an exit of \p C if and only if
  /// \p Target itself is *not* a member of \p C -- exactly what
  /// `CI.contains` answers, in O(1), via a live per-block cycle-membership
  /// lookup (the same one `isInCycle` above already relies on). Two earlier
  /// approaches were tried and rejected for this same call:
  ///  1. A live `CI.getExitBlocks(C, Exits)` call: per `GenericCycleInfo`'s
  ///     own implementation, this walks \p C's frozen, `compute()`-time
  ///     block snapshot and calls `successors()` on every block in it --
  ///     including any block a previously-linearized child or sibling
  ///     cycle has since `eraseFromParent()`'d, a genuine use-after-free.
  ///     Confirmed (via a real crash) to cause `DiamondFlattener::flatten`'s
  ///     own `for (;;)` walk to never terminate on a 3-deep nested loop
  ///     (`dEQP-VK.graphicsfuzz.cosh-return-inf-unused`) once
  ///     `LoopLinearizer::linearizeCyclePostOrder` began attempting a
  ///     non-leaf cycle.
  ///  2. A precomputed, once-at-`run()`-start cache of every cycle's exit
  ///     blocks (avoiding the use-after-free above by never touching a
  ///     freed block's `successors()`): still wrong, because
  ///     `LoopLinearizer::linearizeCycle` itself *inserts new blocks* into
  ///     a cycle's body while restructuring it (masked continue/break
  ///     guards, relay hops, critical-edge splits) -- by the time a
  ///     *parent* cycle is finally attempted (post-order, children first),
  ///     its body contains blocks that did not exist when the cache was
  ///     built, so any exit path reached via one of *those* new blocks is
  ///     simply absent from the stale, pre-mutation cache. Confirmed via
  ///     targeted tracing: for the same 3-deep reproducer, a genuine
  ///     loop-exit-guard block's own exit edge was misclassified because
  ///     its cycle's cached exit list was empty (built before that guard
  ///     block existed), causing the identical non-termination as (1).
  /// The `CI.contains`-based check below is immune to both: it performs no
  /// block-list walk at all (nothing to free out from under it), and
  /// answers against \p Target's *live*, current cycle membership rather
  /// than any frozen snapshot, so a newly-inserted block is classified
  /// correctly the moment it exists (in or out of \p C, exactly as
  /// `LoopLinearizer` itself set it up).
  ///
  /// Roadmap L434: `CI.contains` above is *not* immune to every kind of
  /// staleness -- only to the use-after-free/missing-new-block hazards
  /// its own comment describes. `CI` itself (see `LinearizePass::run`) is
  /// computed exactly once per function, before `LoopLinearizer` attempts
  /// any cycle, and is never recomputed afterward (unlike `DT`/`PDT`,
  /// recalculated before every single `linearizeCycle` call -- see its
  /// own comment); a block `CI.contains` still reports as a member of a
  /// *child* cycle `C` can, by the time an *enclosing* cycle's own later
  /// `canFlattenLoopBodyRegion` precheck walks through it, have already
  /// had its own successor edge rewired by that very child's own already-
  /// committed `linearizeCycle` call -- specifically,
  /// `foldRedundantFlowBlocksInCycle`'s own redundant-`Flow`-dispatch
  /// elimination (safe and correct for a provably-uniform loop with no
  /// mask-affecting op anywhere in it, needing no loop-carried mask at
  /// all) redirects a `Flow` block's own predecessors (e.g. a `.Flow_
  /// crit_edge`-style relay reached only from the loop's own exit check)
  /// directly to their respective successors, bypassing `Flow` -- and, for
  /// the exit-bound predecessor specifically, that successor is the
  /// cycle's own exit block, reached *without* ever looping back to
  /// `Header` again. `CI`'s frozen snapshot has no way to see that this
  /// once-interior relay block can no longer reach `Header` -- it is
  /// still exactly where `compute()` first found it, dominated by
  /// `Header` just as before (dominance, built on reachability from the
  /// entry, is unaffected by a successor edge changing further down), so
  /// `CI.contains` still answers "yes, a member" even though the live
  /// edge graph no longer agrees. Confirmed live-tracing
  /// `dEQP-VK.synchronization.op.single_queue.barrier.write_image_
  /// compute_read_image_compute`'s own two-deep nested uniform loop: the
  /// *outer* loop's own body-region precheck walks straight into the
  /// *already-linearized inner* loop's own now-Flow-collapsed exit check,
  /// misreading its genuine (now even more directly expressed) exit edge
  /// as an ordinary in-cycle edge and misclassifying the whole exit-check
  /// block as an ordinary two-way diamond instead of a cycle boundary --
  /// `validate`'s own diamond handling then computes that block's
  /// immediate post-dominator as the exit edge's own target itself
  /// (structurally correct: every path back through the loop must still
  /// pass through it before reaching the function's single `ret`, exactly
  /// the shape an ordinary loop's exit block always has relative to its
  /// own exit check), tripping the "empty diamond arm" diagnostic the
  /// moment one arm turns out to equal that reconvergence point.
  /// `canReachBlock` below re-verifies reachability live, against the
  /// *current* edge graph, exactly like `DT`/`PDT`'s own recompute already
  /// does for dominance -- but only as a fallback, after `CI.contains`
  /// already said "yes": every block genuinely still a cycle member can,
  /// by that membership's own definition (reachable from `Header` *and*
  /// able to reach `Header`, the same strongly-connected-component
  /// criterion `GenericCycleInfo` itself builds from), still reach
  /// `Header` again, so this fallback costs a bounded walk only for the
  /// genuinely-exited case this comment describes, never misclassifies an
  /// ordinary still-interior edge.
  bool isLoopControlEdge(BasicBlock *Cur, BasicBlock *Target) {
    CycleRef C = CI.getCycle(Cur);
    if (Target == CI.getHeader(C))
      return true;
    if (!CI.contains(C, Target))
      return true;
    return !canReachBlock(Target, CI.getHeader(C), C);
  }

  /// Roadmap L434: a plain, visited-set-bounded forward walk over \p
  /// From's successors, answering whether \p To is reachable at all --
  /// see `isLoopControlEdge`'s own comment for the one, deliberately
  /// narrow case this exists for (re-verifying a cycle membership answer
  /// `CI`'s frozen snapshot can no longer be trusted for on its own).
  /// Confined to blocks `CI.contains(C, ...)` still reports as members of
  /// \p C -- without this, a block whose own exit edge was severed from
  /// \p C's interior (the exact staleness this exists to re-detect) could
  /// still reach \p C's header by a completely different route having
  /// nothing to do with \p C at all (e.g. a sibling/enclosing loop's own
  /// later iteration re-entering the same header block for an unrelated
  /// reason), producing a false "still a member" answer right back.
  bool canReachBlock(BasicBlock *From, BasicBlock *To, CycleRef C) {
    if (From == To)
      return true;
    SmallPtrSet<BasicBlock *, 16> Visited;
    SmallVector<BasicBlock *, 16> Worklist{From};
    Visited.insert(From);
    while (!Worklist.empty()) {
      BasicBlock *BB = Worklist.pop_back_val();
      for (BasicBlock *Succ : successors(BB)) {
        if (Succ == To)
          return true;
        if (!CI.contains(C, Succ))
          continue;
        if (Visited.insert(Succ).second)
          Worklist.push_back(Succ);
      }
    }
    return false;
  }

  /// Roadmap L292: whether \p Cur is its own cycle's header -- gates
  /// `isLoopControlEdgeThroughRelay`'s relay-chase below to *only* a
  /// header's own branch (see that method's own comment for why: an
  /// ordinary non-header body block's branch must always be judged solely
  /// by `isLoopControlEdge`'s own direct-edge test, letting `flatten`'s
  /// normal recursive descent discover any real boundary further down a
  /// relay chain on its own, exactly where it actually is -- chasing
  /// ahead for a non-header block regressed both
  /// `PreservesRedundantFlowBlockWhoseMaskPhiEscapesToOuterDiamond` and
  /// `LinearizesLoopWithTwoRelayHopsToDivergentExit`, each of which
  /// depends on this pass stopping exactly at the *first* block whose own
  /// direct edge reaches outside the cycle, not pre-emptively at some
  /// non-header ancestor merely because one of its arms eventually, after
  /// a few more hops, gets there too).
  bool isCycleHeader(BasicBlock *Cur) {
    CycleRef C = CI.getCycle(Cur);
    return C.isValid() && CI.getHeader(C) == Cur;
  }

  /// Roadmap L292: a bounded, multi-hop generalization of
  /// `isLoopControlEdge` for exactly the shape its own comment describes:
  /// the structurizer sometimes inserts a *chain* of extra relay/dispatch
  /// blocks (possibly more than one, see `dEQP-VK.glsl.demote.
  /// dynamic_loop_always`'s own two-hop `.Flow_crit_edge` -> `Flow` chain)
  /// between a header and the cycle's real continue/exit edge -- e.g. a
  /// shared `Flow`-style block that also receives the backedge -- so \p
  /// Target itself (and possibly several further hops past it) is an
  /// ordinary in-cycle block by `isLoopControlEdge`'s own direct-edge
  /// test, even though the chain as a whole eventually reaches a real
  /// loop control edge. This chases through \p Target and however many
  /// further *pure relay* blocks follow it (`isPureRelayBlock` -- never a
  /// real body block, so this can never misread an ordinary nested
  /// diamond or nested cycle as part of such a chain), stopping (and
  /// returning false) the moment a non-relay block is reached, or \p
  /// Depth exceeds a small bound (the same "no real shader needs this
  /// many relay hops chained together" reasoning as `validate`'s own
  /// `Depth` parameter). Only ever called when `isCycleHeader(Cur)` holds
  /// (see that method's own comment for why this must not apply to an
  /// ordinary non-header body block's branch).
  ///
  /// \p StopAt is `Cur`'s own immediate post-dominator (computed once by
  /// `isLoopControlOrHeaderRelayEdge`, the only caller) -- the natural
  /// reconvergence point of `Cur`'s own two-way branch. The chase must
  /// stop (returning false) the moment it would walk *through* that
  /// block rather than past it: reaching it means `Cur`'s branch is an
  /// ordinary internal diamond whose arms both rejoin before the loop's
  /// real exit decision happens somewhere further downstream (confirmed
  /// by `loop-break-structurized.ll`'s own early-`break` diamond, whose
  /// reconvergence block -- itself a pure relay, since it holds only the
  /// merge phis -- otherwise looked identical, by this chase's own
  /// pre-existing logic, to a genuine one-more-hop path to the loop's
  /// real exit, wrongly treating the break diamond itself as the loop's
  /// own iteration decision and leaving it entirely unflattened).
  ///
  /// Roadmap L292: reaching `StopAt` does not, by itself, distinguish
  /// those two shapes from a *third*: `StopAt` can simultaneously be both
  /// `Cur`'s own natural reconvergence point *and* the structurizer's
  /// shared dispatch block for the loop's real continue/exit decision
  /// (confirmed on `dEQP-VK.glsl.demote.dynamic_loop_always`'s own
  /// `Header` -> `.Flow_crit_edge` -> `Flow` chain, where `Flow`'s own
  /// branch -- not some further hop past it -- is the one with a genuine
  /// edge leaving the cycle). Unlike `loop-break-structurized.ll`'s own
  /// shape (whose reconvergence's two successors both stay inside the
  /// cycle, deferring the real exit decision further downstream still),
  /// `Flow` here has a direct escaping successor itself. So `StopAt` is
  /// checked once, directly (via `isLoopControlEdge`, never another
  /// recursive chase past it -- that would reintroduce the exact
  /// misreading the `StopAt` bound exists to prevent) for a genuine
  /// escape among *its own* immediate successors before this chase
  /// refuses to go any further.
  bool isLoopControlEdgeThroughRelay(BasicBlock *Cur, BasicBlock *Target,
                                     BasicBlock *StopAt, unsigned Depth = 0) {
    if (isLoopControlEdge(Cur, Target))
      return true;
    // Roadmap L419(u): `Target == StopAt` must be checked *before* the
    // pure-relay gate below, not after it. `StopAt` is `Cur`'s own
    // natural reconvergence point, not just another relay hop -- it is
    // legitimately allowed to contain real instructions of its own (most
    // commonly the loop's actual trip-count/exit-condition `icmp`) ahead
    // of its branch, unlike every other hop this chase walks through.
    // Gating on `isPureRelayBlock(Target)` first wrongly disqualified
    // exactly that common shape (confirmed on
    // `dEQP-VK.glsl.loops.special.do_while_dynamic_iterations.select_
    // iteration_count_{fragment,vertex}`, whose Header-internal if/else
    // reconverges at a block computing its own real `icmp` before
    // branching to the loop's backedge/exit) -- silently treating the
    // Header's own internal diamond as an ordinary, fully-flattenable
    // one instead of a cycle boundary to defer to `LoopLinearizer`,
    // which flattened it with the wrong (pre-masking) verdict.
    if (Target == StopAt) {
      Instruction *Term = Target->getTerminator();
      if (auto *UBr = dyn_cast<UncondBrInst>(Term))
        return isLoopControlEdge(Cur, UBr->getSuccessor(0));
      if (auto *CBr = dyn_cast<CondBrInst>(Term))
        return isLoopControlEdge(Cur, CBr->getSuccessor(0)) ||
               isLoopControlEdge(Cur, CBr->getSuccessor(1));
      return false;
    }
    if (Depth >= 8 || !isPureRelayBlock(Target))
      return false;
    Instruction *Term = Target->getTerminator();
    if (auto *UBr = dyn_cast<UncondBrInst>(Term))
      return isLoopControlEdgeThroughRelay(Cur, UBr->getSuccessor(0), StopAt,
                                           Depth + 1);
    if (auto *CBr = dyn_cast<CondBrInst>(Term))
      return isLoopControlEdgeThroughRelay(Cur, CBr->getSuccessor(0), StopAt,
                                           Depth + 1) ||
             isLoopControlEdgeThroughRelay(Cur, CBr->getSuccessor(1), StopAt,
                                           Depth + 1);
    return false;
  }

  /// Roadmap L292: the actual boundary-stop predicate every caller below
  /// uses -- a plain direct loop control edge from \p Cur to \p Target,
  /// or (only when \p Cur is itself a cycle header -- see
  /// `isCycleHeader`'s own comment) one reached via
  /// `isLoopControlEdgeThroughRelay`'s bounded relay chase instead, which
  /// is itself bounded from reaching past `Cur`'s own reconvergence point
  /// (see that method's own comment for why).
  bool isLoopControlOrHeaderRelayEdge(BasicBlock *Cur, BasicBlock *Target) {
    if (isLoopControlEdge(Cur, Target))
      return true;
    return isCycleHeader(Cur) &&
           isLoopControlEdgeThroughRelay(Cur, Target, immediatePostDom(Cur));
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
  /// loop. When \p Quiet is set (see `flattenLoopBodyDiamond`), every
  /// would-be diagnostic is suppressed and this simply returns false
  /// instead -- used when a failure here is an ordinary "not this shape"
  /// result for a different, later check to try instead, not a genuine
  /// compile error.
  ///
  /// Roadmap L282: \p Depth (never set explicitly by an external caller;
  /// each recursive self-call below increments it) bounds this function's
  /// own recursive-descent depth -- see the definition's own comment on
  /// `dEQP-VK.graphicsfuzz.increment-value-in-nested-for-loop`'s real,
  /// confirmed `SIGSEGV` (a genuine stack overflow, not a hang) once
  /// `LoopLinearizer` began attempting a non-leaf cycle. Whatever
  /// structural cause actually drives some shapes into runaway recursion
  /// here (not yet root-caused -- see roadmap `L277`'s own "bug 5"
  /// writeup) is irrelevant to this guard: any recursion this deep is
  /// categorically not a diamond-nesting depth a real shader could ever
  /// need (ordinary GLSL/HLSL control flow nests at most a few dozen
  /// diamonds deep, never thousands), so treating it exactly like any
  /// other "not this shape" validation failure -- a clean diagnostic, not
  /// a crash -- is always safe, never a false rejection of real shader
  /// code.
  ///
  /// Roadmap L375 (6th investigation): detects a block that is a genuine
  /// diamond-start (an ordinary, non-cycle-boundary two-way branch)
  /// reached more than once across *different* roots `run` processes --
  /// e.g. one root's own reconvergence point happening to also be a
  /// *different*, earlier root's own later diamond-start, found via live
  /// tracing on `dEQP-VK.spirv_assembly.instruction.graphics.loop.
  /// switch_continue_{geom,tessc,tesse}`'s own `Flow26.Flow27_crit_edge`
  /// block (reached once as `run`'s own cycle-exit root for one cycle,
  /// and again mid-walk from an entirely different, earlier root, via two
  /// structurally-unrelated paths neither this function's own single-call
  /// recursion nor the entry-root/cycle-exit-root split in `run` has any
  /// way to see are the same physical block until it is too late).
  /// `flatten` (see its own comment on `CycleBoundaryMasks`) already
  /// tolerates exactly this shape for a *cycle-boundary* block, where it
  /// is an expected, correctly-handled convergence of an enclosing
  /// diamond's own arm and a nested cycle's own exit root (see `RootEnd`)
  /// -- but an *ordinary* (non-cycle-boundary) merge block has no such
  /// handling: a second walk reaching it re-derives its own reconvergence
  /// point and re-flattens it from scratch, silently stealing a
  /// predecessor edge from the first walk's already-finalized `phi`s and
  /// tripping `flatten`'s own "exactly two predecessors" assertion later.
  /// `R->hasNPredecessors(2)` above already guards a diamond's own
  /// *reconvergence* point against an unexpected third predecessor; this
  /// is the missing, symmetric guard against a block (any block this
  /// walk visits -- not only a divergent diamond's own start, see
  /// `L383(a)`'s 7th investigation) being an unexpected second entry into
  /// territory some other walk already claimed. A plain pass-through
  /// block (an ordinary `UncondBr` link in one arm's chain) can be
  /// exactly such territory too: found live-traced as a block reached
  /// both partway down an outer diamond's own arm chain *and* directly as
  /// a nested diamond's own branch successor (found later, within the
  /// *other* arm), where both diamonds' own `R` happened to coincide --
  /// each arm's own `hasNPredecessors(2)` check at its own `R` passes
  /// individually (each only ever sees the 2 physical edges actually
  /// feeding that shared `R`), so only tracking every visited block here,
  /// not just `CondBr`-terminated diamond starts, catches the overlap
  /// before `flatten` later steals one of those edges out from under the
  /// other arm's own, not-yet-processed merge. Tracked in `MergeVisited`
  /// -- a per-instance member, deliberately *not* reset between roots the
  /// way `CycleBoundaryBlocks` is (see `run`'s own loop) -- rather than a
  /// parameter threaded only through one top-level call's own recursion,
  /// since (as the live trace above shows) the collision is not confined
  /// to a single top-level call's own arms.
  bool validate(BasicBlock *Start, BasicBlock *End, bool Quiet = false,
               unsigned Depth = 0);

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

  /// Roadmap L375 (6th investigation), broadened by L383(a) (7th
  /// investigation): every ordinary (non-cycle-boundary) block `validate`
  /// has visited so far -- originally only diamond-start (`CondBr`)
  /// blocks, now every block the walk passes through (see `validate`'s
  /// own comment for why a plain pass-through `UncondBr` link needs the
  /// same guard) -- a *reference* to state owned by
  /// `LinearizePass::run`'s own per-`Function` driver (see its own
  /// comment), shared across every `DiamondFlattener` instance
  /// constructed for that one `Function` (this whole-function pass's own
  /// instance, and every one `LoopLinearizer` separately constructs
  /// internally for a single cycle or candidate block) -- *not* a member
  /// owned by any single instance, since the live-traced collision this
  /// exists to reject (see `validate`'s own comment) can span two
  /// entirely different, sequentially-constructed-and-destroyed
  /// `DiamondFlattener` objects, not just two roots within one object's
  /// own `run()`. Unlike `CycleBoundaryBlocks` below, deliberately never
  /// cleared for the lifetime of that per-`Function` driver.
  SmallPtrSetImpl<BasicBlock *> &MergeVisited;

  /// Roadmap L425: every block some *earlier*, already fully-committed
  /// top-level `flattenLoopBodyDiamond`/`flattenLoopBodyRegion` call
  /// actually flattened (not merely visited/rejected) -- a subset of
  /// `MergeVisited`, same whole-`Function` lifetime and sharing, but
  /// tracking a strictly narrower fact: "this block's own diamond, if it
  /// had one, is already fully resolved; nothing here can still be
  /// independently claimed by a different candidate." `validate` uses
  /// this to tell apart two shapes that otherwise look identical through
  /// `MergeVisited` alone -- a genuine `L375`-style conflict (two still-
  /// uncommitted candidates racing for the same block, which must still
  /// fail) versus a later, larger region's walk simply passing back
  /// through a block an earlier, *separate* candidate already flattened
  /// and committed (safe: nothing is being claimed a second time, the
  /// walk just needs to continue past it to reach its own `End`).
  /// Confirmed necessary via `AllMemoryBarrierWithGroupSync.test`'s own
  /// `for` loop: its body's inner `if (GTID.x == I)` diamond is flattened
  /// first, as an ordinary loop-body diamond (see the `flattenLoopBodyDiamond`
  /// fixed-point loop in `LoopLinearizer::linearizeCycle`), strictly
  /// *before* the loop's own, separate `flattenLoopBodyRegion` call walks
  /// the whole `[HeaderStayInLoop, Latch)` region (which contains that
  /// same, by-then-already-flattened diamond's blocks) -- without this
  /// set, the second walk spuriously treated its own first block as an
  /// unresolvable conflict, rejecting an otherwise fully supported loop
  /// shape outright.
  SmallPtrSetImpl<BasicBlock *> &ResolvedBlocks;

  /// Roadmap L447: see the constructor parameter's own comment -- true
  /// only for the single instance `run()`'s whole-function-pass caller
  /// builds, gating the "defer an in-cycle mask-op diamond to
  /// `LoopLinearizer`" check in `validate`/`flatten`.
  bool IsWholeFunctionPass;

  /// Roadmap L425: every block the *current*, still-in-progress top-level
  /// `flattenLoopBodyDiamond`/`flattenLoopBodyRegion` call has freshly
  /// added to `MergeVisited` so far -- local to this one call (cleared at
  /// its start), not shared across instances or calls like `MergeVisited`/
  /// `ResolvedBlocks` above. Folded into `ResolvedBlocks` once (and only
  /// if) that call's own `flatten` actually commits successfully, so a
  /// later, different call can safely walk through these same blocks
  /// without re-triggering `MergeVisited`'s conflict check.
  SmallPtrSet<BasicBlock *, 16> LastRegionVisited;

  /// Roadmap H95a: for every block `flatten` itself stopped at (the same
  /// boundary `CycleBoundaryBlocks` records during `validate`, see above),
  /// the `MaskPair` that was actually in effect there -- i.e. the mask
  /// describing whether the invocation reaching that block at all is live,
  /// not merely "every invocation" -- so a cycle-exit root reached only
  /// from inside some still-divergent enclosing region (e.g. a loop nested
  /// in `if (laneId == 0) { ... }`) can be seeded with that real mask
  /// instead of unconditionally assuming every lane reaches it (see `run`).
  /// Roadmap L440: `WeakMaskPair`-valued (not plain `MaskPair`) because
  /// entries here are read back well after they are recorded -- both
  /// later in this same pass's own `Roots` walk, and, via
  /// `getCycleBoundaryMasks()`, by a wholly separate `LoopLinearizer` run
  /// afterward -- by which point unrelated IR mutation may have erased
  /// the instruction a stale raw `Value *` would otherwise still (and
  /// incorrectly) point at.
  DenseMap<BasicBlock *, WeakMaskPair> CycleBoundaryMasks;

  /// Roadmap H95a: the reverse of the mapping `run` builds from
  /// `CycleBoundaryBlocks` to each cycle's exit block(s) -- for a given
  /// exit-block root, every boundary block whose walk reached it, so `run`
  /// can look up (via `CycleBoundaryMasks`) every mask that reaches that
  /// root and use it directly when they all agree (see `run`).
  DenseMap<BasicBlock *, SmallVector<BasicBlock *, 2>> ExitToBoundaryBlocks;

  /// Roadmap L151: for every block `validate` stopped at (see
  /// `CycleBoundaryBlocks`), the reconvergence point (`End`, possibly
  /// `nullptr` for "the rest of the function") that walk was validating
  /// towards when it stopped -- i.e. what a nested cycle's own exit-block
  /// root must eventually reach before some *other*, enclosing root's own
  /// walk takes over (see `RootEnd`). Like `CycleBoundaryMasks`, never
  /// cleared between roots and keyed on whichever walk reaches a given
  /// boundary block first, since that is the same walk `run` uses to seed
  /// `RootEnd` for the boundary block's own exit root(s) immediately
  /// after recording it.
  DenseMap<BasicBlock *, BasicBlock *> CycleBoundaryEnd;

  /// Roadmap L151: for every cycle-exit block `run` has added as its own
  /// root, the reconvergence point (from `CycleBoundaryEnd`) that root's
  /// own `flatten` call must stop at -- `nullptr` (the default a missing
  /// key's lookup yields) for the common case of a cycle not nested in
  /// any enclosing diamond's arm, in which case that root's own walk
  /// correctly continues all the way to a `ret`, exactly as before this
  /// map existed. Without this, an exit root nested inside an enclosing
  /// diamond's arm would walk *past* that diamond's own merge point and
  /// re-mask its (already correctly merged, by the enclosing diamond's
  /// own root) downstream code with this root's own narrower, stale mask
  /// instead -- see the file's `L151` roadmap entry for the miscompile
  /// this caused.
  DenseMap<BasicBlock *, BasicBlock *> RootEnd;

  /// Roadmap H75: every masked-load result `applyStageMasks` has produced
  /// so far, across every root `flatten` has processed in this `run` --
  /// see `applyStageMasks`'s own `MaskedLoads` parameter comment, and
  /// `dependsOnTaintedValue`'s use of this set in `flatten`'s divergent-
  /// vs-uniform branch classification, for why this needs tracking at all.
  /// Deliberately never cleared between roots: a value from one root's own
  /// walk can only reach a *later* root's branch condition through genuine
  /// dominance/data flow (e.g. a cycle-exit root reading a value a masked
  /// load before that cycle computed), in which case it is exactly as
  /// tainted there as it was where it was created.
  SmallPtrSet<Value *, 16> MaskedLoadResults;
};

bool DiamondFlattener::validate(BasicBlock *Start, BasicBlock *End,
                                bool Quiet, unsigned Depth) {
  // Roadmap L282: see this method's own declaration comment for why this
  // bound exists at all -- 256 is generous next to any real diamond
  // nesting depth a shader could plausibly have, while still being far
  // short of a stack-overflow-inducing depth.
  constexpr unsigned MaxValidateDepth = 256;
  if (Depth >= MaxValidateDepth) {
    if (!Quiet)
      diagnose(F, "divergent region starting at '" + Start->getName() +
                      "' is nested too deeply to validate (roadmap "
                      "milestone 6 deviation)");
    return false;
  }
  BasicBlock *Cur = Start;
  while (Cur != End) {
    Instruction *Term = Cur->getTerminator();
    if (isa<ReturnInst>(Term)) {
      if (End != nullptr) {
        if (!Quiet)
          diagnose(F, "early return under a divergent branch is not yet "
                      "supported (roadmap milestone 6 deviation)");
        return false;
      }
      return true;
    }

    auto *Br = dyn_cast<CondBrInst>(Term);

    // Roadmap L151: a cycle-boundary exemption must be checked before
    // `MergeVisited` below, since a cycle's own exit edge may legitimately
    // be revisited by more than one walk (see `CycleBoundaryBlocks`'s own
    // comment for why).
    if (Br && isInCycle(Cur) &&
        (isLoopControlOrHeaderRelayEdge(Cur, Br->getSuccessor(0)) ||
         isLoopControlOrHeaderRelayEdge(Cur, Br->getSuccessor(1)))) {
      // Roadmap K6z: before treating this as an obstruction, check
      // whether `Cur` is a nested sibling cycle's own header that
      // `LoopLinearizer`'s post-order already fully linearized *before*
      // this call (only ever possible when the caller supplied
      // `GetAlreadyLinearizedCycleExit` -- see its own comment) -- the
      // shape `matrix_single_subscript_load.test`'s own outer uniform
      // `for` loop has, with a genuinely separate, already-closed nested
      // `for` loop sitting earlier in its body rather than the loop's own
      // exit check at the tail (the only shape the `CycleBoundaryBlocks`
      // tolerance below was originally built for). Such a cycle's own
      // already-finalized interior is never this walk's concern; treat
      // it as fully transparent and keep validating from its one exit
      // block onward to `End` instead of stopping here at all.
      if (GetAlreadyLinearizedCycleExit) {
        if (BasicBlock *ExitBB = GetAlreadyLinearizedCycleExit(Cur))
          return validate(ExitBB, End, Quiet, Depth + 1);
      }
      CycleBoundaryBlocks.insert(Cur);
      // Remember the reconvergence point (if any) this walk was headed for
      // when it stopped here, so `run` can seed this cycle's own
      // exit-block root with the *same* one (see `RootEnd`) instead of
      // always assuming the cycle's exit reaches a `ret` directly. A cycle
      // nested inside an enclosing diamond's arm (e.g. a second, sibling
      // loop inside an `if`'s true arm) has its exit block's own
      // downstream code -- up to and including that diamond's merge --
      // *shared* with the enclosing diamond's other arm; without this,
      // the exit-block root's own `flatten` walk would independently
      // re-walk (and re-mask, with a stale, narrower mask) that same
      // shared tail after the enclosing root's walk already merged and
      // masked it correctly.
      CycleBoundaryEnd.try_emplace(Cur, End);
      return true; // Stop here; LoopLinearizer's problem, not an error.
    }

    // Roadmap L447: a divergent branch strictly inside a cycle that is
    // *not* itself that cycle's own loop-control edge (the case just
    // above) can still be a genuine loop-body diamond --
    // `LoopLinearizer::linearizeCycle`'s own fixed-point
    // `flattenLoopBodyDiamond` loop already knows how to flatten exactly
    // this shape safely, deferring (via `regionHasStageMaskOps`, the same
    // gate `flattenLoopBodyDiamond` itself already uses) whenever the
    // diamond contains a side-effecting resource/image call or a
    // `feme.stage.*` mask op, so the real per-iteration "active" mask
    // (not yet known this early) can narrow it instead of a placeholder.
    // Without this check, this whole-function walk -- which runs before
    // `LoopLinearizer` even starts, and has no other check of this kind --
    // reaches (and incorrectly flattens) such a diamond first, baking in
    // a mask derived only from this walk's own `AllActive` seed with zero
    // contribution from the loop's own iteration bound. Confirmed via
    // roadmap `L447`'s own writeup to be the exact root cause of `L442`'s
    // `max_workgroup_memory` exact-2x doubling: an inner loop's own
    // out-of-bounds ghost iteration's atomic fired ungated by the loop's
    // bound because this diamond (containing the atomic) was flattened
    // here, before `LoopLinearizer` ever computed that bound's mask.
    // Deliberately conservative: only takes this path when `R` is a valid
    // reconvergence point, so a malformed diamond here still falls
    // through to the ordinary validation below and gets a real
    // diagnostic rather than being silently skipped. A diamond that does
    // *not* contain a mask op is left to this pass's own ordinary
    // flattening immediately below, exactly as `run()`'s own doc comment
    // says it should be -- this check only narrows that behavior for the
    // specific shape that needs deferring.
    //
    // Gated on `IsWholeFunctionPass` (see its own comment): this
    // `validate` method is also called recursively by
    // `flattenLoopBodyDiamond`'s and `flattenLoopBodyRegion`'s own
    // `LoopLinearizer::linearizeCycle` call sites -- i.e. by the very
    // mechanism this check exists to defer *to*. Applying this same
    // defer unconditionally inside those recursive calls would make them
    // defer the diamond right back to themselves, so it would never
    // actually get flattened at all (confirmed live: `feme-cpu-simdize`
    // then rejects the still-untouched divergent branch outright, rather
    // than `L442`'s CTS case actually being fixed).
    if (IsWholeFunctionPass && Br && isInCycle(Cur)) {
      if (BasicBlock *R = immediatePostDom(Cur)) {
        if (regionHasStageMaskOps(Cur, R)) {
          Cur = R;
          continue;
        }
      }
    }

    // Roadmap L375/L383(a): see `MergeVisited`'s own comment for the exact
    // shape this rejects -- any block (not only a divergent diamond-start
    // block, see L383(a)'s 7th investigation) that some *other*,
    // already-processed walk already claimed as its own. Checked here
    // (after the cycle-boundary early return above has already had its
    // chance to accept a legitimate multi-visit) and keyed on `Cur`
    // itself, not `R`: `R->hasNPredecessors(2)` below only catches a
    // reconvergence point with an unexpected third predecessor at the
    // moment its owning diamond is validated, not a plain pass-through
    // block reachable from two different arms that happens to merge back
    // into the same reconvergence point from both -- e.g. an outer
    // diamond's own arm chain passing through a block that a nested
    // diamond (found later, within the *other* arm) also branches to
    // directly as one of its own two successors. Both arms' own
    // `hasNPredecessors(2)` checks pass individually (each only ever sees
    // the 2 physical edges actually feeding their own shared `R`), so the
    // overlap is invisible unless the shared interior block itself is
    // tracked, which is what moving this check here (covering every
    // visited block, not only `CondBr`-terminated diamond starts)
    // accomplishes.
    //
    // Roadmap L425: a block already in `MergeVisited` is *not* always this
    // conflict -- see `ResolvedBlocks`'s own comment. If an earlier,
    // separate, already-fully-committed call already flattened it, there
    // is nothing left here to double-claim; just continue the walk
    // through its (by-now-mutated) current successor instead of failing.
    if (MergeVisited.contains(Cur) && !ResolvedBlocks.contains(Cur)) {
      if (!Quiet)
        diagnose(F, "block '" + Cur->getName() +
                        "' is reached by more than one divergent root "
                        "(a shared merge point outside a cycle boundary); "
                        "unsupported (roadmap milestone 6 deviation, L375)");
      return false;
    }
    if (MergeVisited.insert(Cur).second)
      LastRegionVisited.insert(Cur);

    if (isa<ReturnInst>(Term)) {
      if (End != nullptr) {
        if (!Quiet)
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

    if (!Br) {
      if (!Quiet)
        diagnose(F, "unsupported terminator '" +
                        Twine(Term->getOpcodeName()) +
                        "' in a linearizable region");
      return false;
    }

    BasicBlock *T = Br->getSuccessor(0);
    BasicBlock *Fsucc = Br->getSuccessor(1);

    BasicBlock *R = immediatePostDom(Cur);
    if (!R) {
      if (!Quiet)
        diagnose(F, "divergent branch in '" + Cur->getName() +
                        "' has no reconvergence point");
      return false;
    }
    if (T == R || Fsucc == R) {
      if (!Quiet)
        diagnose(F, "an empty diamond arm (in '" + Cur->getName() +
                        "') is not yet supported (roadmap milestone 6 "
                        "deviation)");
      return false;
    }
    // The reconvergence block must have exactly the two predecessors this
    // rewrite expects to redirect/select between; anything else is a merge
    // shape this milestone does not generalize to yet.
    if (!R->hasNPredecessors(2)) {
      if (!Quiet)
        diagnose(F, "reconvergence block '" + R->getName() +
                        "' does not have exactly two predecessors");
      return false;
    }

    if (!validate(T, R, Quiet, Depth + 1) ||
        !validate(Fsucc, R, Quiet, Depth + 1))
      return false;
    Cur = R;
  }
  return true;
}

std::optional<BasicBlock *>
DiamondFlattener::flattenLoopBodyDiamond(BasicBlock *Start) {
  if (!isa<CondBrInst>(Start->getTerminator()))
    return std::nullopt;
  BasicBlock *R = immediatePostDom(Start);
  if (!R)
    return std::nullopt;
  // A clean `validate` that never records a cycle-boundary block means
  // both arms genuinely reconverge at `R` without crossing a loop control
  // edge -- exactly the "safe, ordinary diamond" shape this method exists
  // to flatten. If it instead stopped early at such an edge (returning
  // true anyway; see `validate`'s own comment), that is NOT this shape:
  // treat it the same as an outright validation failure.
  CycleBoundaryBlocks.clear();
  LastRegionVisited.clear();
  if (!validate(Start, R, /*Quiet=*/true) || !CycleBoundaryBlocks.empty()) {
    // Roadmap L432(a): a successful `validate` whose walk stopped at a
    // cycle-boundary edge (the case just rejected above) still inserts
    // every block it visited into the *shared* `MergeVisited` -- exactly
    // the same bookkeeping hazard `regionHasStageMaskOps`'s own rollback
    // further below exists to undo -- but, unlike that case, had no
    // rollback of its own here: this declined trial's own leftover
    // `MergeVisited` entries (tracked in `LastRegionVisited`) would
    // otherwise permanently and incorrectly conflict against any later,
    // *legitimate* walk over the same blocks (the `L375` "reached by more
    // than one divergent root" diagnostic firing as a false positive) --
    // found reproducing `dEQP-VK.synchronization.op.*.write_blit_image_
    // read_image_compute.image_128_r32_uint_concurrent`'s own uniform
    // loop, whose real exit check this fixed-point pre-pass tries (and
    // correctly declines) to flatten as an ordinary diamond before
    // `LoopLinearizer::linearizeCycle`'s own, later `flattenLoopBodyRegion`
    // call legitimately needs to walk straight through the very same
    // blocks.
    for (BasicBlock *BB : LastRegionVisited)
      MergeVisited.erase(BB);
    return std::nullopt;
  }
  // Roadmap L292: this method always seeds its own walk with a bare,
  // unconditional "every lane active" pair (see below) -- correct for an
  // ordinary diamond with no mask-affecting op of its own (flattening it
  // early here is a harmless simplification, see this method's own
  // caller), but wrong whenever the diamond itself contains a
  // `feme.stage.discard`/`.demote`/etc.: that op's effect must instead
  // accumulate against this cycle's *real*, loop-carried mask (see
  // `flattenLoopBodyRegion`), not a fresh always-active guess reset on
  // every iteration. Defer entirely to the caller in that case (returning
  // `std::nullopt`, the same as any other shape this method does not
  // handle) -- confirmed via `dEQP-VK.glsl.demote.dynamic_loop_dynamic`'s
  // own `if (dynamic condition) demote` nested inside a uniform `if (i >
  // 0)`: flattening it here first, before `LoopLinearizer` ever computes
  // the loop's real mask, silently baked in a wrong, always-"demote never
  // happened before this iteration" guess, corrupting every later
  // iteration's masked output.
  if (regionHasStageMaskOps(Start, R)) {
    // Roadmap L425(a) (3rd bug): the `validate` call just above has
    // already inserted every block its walk visited into the *shared*
    // `MergeVisited` set (tracked in `LastRegionVisited`), on the
    // assumption that a clean `validate` here always leads straight to
    // this diamond being committed via `flatten` below. That assumption
    // just broke: this diamond is being deferred *whole* to
    // `LoopLinearizer`'s own `flattenLoopBodyRegion`, which will call
    // `validate` again over a region that still includes `[Start, R)` --
    // and without rolling back these insertions first, that later, real
    // call immediately (and incorrectly) conflicts against this
    // discarded trial's own leftover entries (the exact
    // `MergeVisited`-without-`ResolvedBlocks` shape `validate`'s L375
    // check exists to catch, firing here as a false positive). Found via
    // `AllMemoryBarrierWithGroupSync.test`'s `for (...) { if (GTID.x ==
    // I) { ...; UavCounter[0]++; } ... }`, whose `UavCounter` store is
    // exactly what makes `regionHasStageMaskOps` return true here, after
    // the 2nd bug's own fix (above) added that check.
    for (BasicBlock *BB : LastRegionVisited)
      MergeVisited.erase(BB);
    return std::nullopt;
  }
  MaskPair AllActive{ConstantInt::getTrue(F.getContext()),
                     ConstantInt::getTrue(F.getContext())};
  flatten(Start, R, AllActive, /*RedirectTo=*/R);
  // Roadmap L197: `flatten` (see its own comment on the uniform-branch
  // case) leaves a branch it judges *not* to be an actual divergent
  // terminator -- `UI.isDivergentTerminator`, plus a masked-load-taint
  // check neither this method's own caller nor `validate` can see --
  // entirely untouched, merely walking past it to keep processing its
  // arms. That notion of "divergent" is `DiamondFlattener`'s own,
  // authoritative one, but it can genuinely disagree with
  // `LoopLinearizer::isDivergentBranch`'s own, broader, `KnownUniform
  // Values`-aware notion that `linearizeCycle`'s "flatten loop body
  // diamond to a fixed point" caller uses to decide whether to *attempt*
  // this method at all (see that method's own comment): a branch
  // `isDivergentBranch` calls divergent (so worth attempting) but
  // `flatten` itself judges uniform (so leaves untouched) is a genuine,
  // real shape -- confirmed via targeted tracing on a real 3-deep nested
  // CTS reproducer (`dEQP-VK.graphicsfuzz.cosh-return-inf-unused`) --
  // not a defensive/theoretical case. Without this check, such a branch
  // is "successfully" flattened (this method returns non-null, since
  // `flatten` genuinely walked and mutated *something* downstream -- new
  // merge `phi`s at `R`, for instance) yet `Start`'s own terminator is
  // never actually replaced, so `linearizeCycle`'s caller finds the exact
  // same divergent-looking branch again on its very next fixed-point
  // sweep, forever: this was the confirmed root cause of a second,
  // distinct infinite hang in `DiamondFlattener::flatten`'s own `for (;;)`
  // walk (after an earlier, different use-after-free/staleness hazard in
  // `isLoopControlEdge` was fixed) once `LoopLinearizer` began attempting
  // a non-leaf cycle. Detecting the no-real-progress case here, by simply
  // checking whether `Start`'s own terminator was actually replaced,
  // lets the caller correctly treat it as "nothing to do" (returning
  // `std::nullopt`, same as any other reason this shape cannot be
  // flattened) instead of an infinitely-repeatable "success".
  if (isa<CondBrInst>(Start->getTerminator()))
    return std::nullopt;
  // Roadmap L425: this diamond is now fully flattened and committed --
  // every block `LastRegionVisited` collected during the `validate` call
  // above is safe for a later, separate region's own walk to pass back
  // through (see `ResolvedBlocks`'s own comment).
  ResolvedBlocks.insert(LastRegionVisited.begin(), LastRegionVisited.end());
  return R;
}

std::optional<MaskPair>
DiamondFlattener::flattenLoopBodyRegion(BasicBlock *Start, BasicBlock *End,
                                        MaskPair Masks) {
  // Mirrors `flattenLoopBodyDiamond`'s own "clean validate, no cycle
  // boundary" precondition (see its comment), just against a
  // caller-supplied `End` (this cycle's own latch) instead of `Start`'s
  // immediate post-dominator -- the region between a loop's own (left
  // alone, uniform) header exit check and its latch is not itself a
  // single two-arm diamond, but an arbitrary straight-line chain of them.
  //
  // Roadmap L292: unlike `flattenLoopBodyDiamond`, a *single* cycle
  // boundary block is tolerated here rather than always rejected -- but
  // only the specific, safe shape `validate`'s own top-level walk (not a
  // nested diamond-arm recursion -- see `CycleBoundaryEnd`) can produce:
  // one of `End`'s own immediate predecessors, reached directly by this
  // same top-level walk, whose own two-way branch is this cycle's real,
  // uniform per-iteration exit check (one successor genuinely escapes the
  // cycle, the other is `End` itself). That block is simply the loop's
  // own natural exit-check sitting at the very tail of `[Start, End)`
  // -- not a diamond needing any flattening at all -- so `flatten`'s own
  // identical boundary-stop check (see its call to
  // `isLoopControlOrHeaderRelayEdge`) already leaves it untouched on its
  // own; nothing here needs to redirect or otherwise special-case it
  // further. Confirmed via `dEQP-VK.glsl.demote.dynamic_loop_always`'s own
  // shape: `StructurizeCFG` routes its loop's real exit check through a
  // shared `Flow`-style dispatch block that is also, incidentally, `End`'s
  // sole real predecessor along this walk -- rejecting that shape
  // outright (the original, over-strict behavior) silently dropped the
  // loop's own `feme.stage.demote` mask-threading entirely, with no
  // diagnostic at all.
  CycleBoundaryBlocks.clear();
  LastRegionVisited.clear();
  if (!validate(Start, End, /*Quiet=*/true))
    return std::nullopt;
  for (BasicBlock *BB : CycleBoundaryBlocks) {
    auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
    bool Tolerable = Br && CycleBoundaryEnd.lookup(BB) == End &&
                      (Br->getSuccessor(0) == End || Br->getSuccessor(1) == End);
    if (!Tolerable)
      return std::nullopt;
  }
  // Roadmap L425: see `ResolvedBlocks`'s own comment -- this region is now
  // fully validated and about to be committed by `flatten` below; release
  // every block this call's own walk visited for any later, separate
  // region's walk to safely pass back through.
  ResolvedBlocks.insert(LastRegionVisited.begin(), LastRegionVisited.end());
  return flatten(Start, End, Masks, /*RedirectTo=*/End);
}

std::optional<BasicBlock *>
DiamondFlattener::flattenKnownDivergentBranch(BasicBlock *Start,
                                              MaskPair Masks) {
  auto *Br = dyn_cast<CondBrInst>(Start->getTerminator());
  if (!Br)
    return std::nullopt;
  BasicBlock *T = Br->getSuccessor(0);
  BasicBlock *Fsucc = Br->getSuccessor(1);
  BasicBlock *R = immediatePostDom(Start);
  if (!R || T == R || Fsucc == R || !R->hasNPredecessors(2))
    return std::nullopt;
  // Roadmap L419(w): if `Start` sits inside some cycle `SC` but `R` does
  // not (i.e. `R` is that cycle's own exit block, or further out still),
  // this is not the short, locally-reconverging diamond this method's own
  // per-arm `TMasks`/`FMasks` are later *discarded* for (see this
  // method's own comment on that) -- one of `Start`'s two arms has
  // genuinely escaped the cycle's own iteration structure (e.g. a
  // divergent `break`), so the narrower mask each arm computes needs to
  // keep propagating forward through the rest of that cycle's own
  // linearization (its backedge phis, any later `PreRegion`/`PostRegion`
  // block, its own exit check) rather than being thrown away the moment
  // this call returns. Silently flattening it anyway produces a
  // plausible-looking but *wrong* CFG: the divergent arm's own real
  // control-flow target is simply replaced by an unconditional branch to
  // the other (masked) arm, permanently discarding the break/early-exit
  // path -- found reducing `dEQP-VK.glsl.loops.special.
  // do_while_uniform_iterations.conditional_break_{fragment,vertex}`'s own
  // image-mismatch regression. Bailing out here instead routes the
  // caller (`LoopLinearizer::flattenTaintedBranchesInRegion`) to its own
  // "does not have the simple shape this pass can flatten" diagnostic,
  // which is the honest, correct outcome until this shape gets its own,
  // mask-propagating support.
  if (CycleRef SC = CI.getCycle(Start); SC.isValid() && !CI.contains(SC, R))
    return std::nullopt;

  // Each arm's own *interior* is still fully subject to the ordinary
  // `validate`/`CycleBoundaryBlocks` rules -- only `Start` itself (never
  // itself part of this walk, see this method's own comment) bypasses
  // them.
  CycleBoundaryBlocks.clear();
  LastRegionVisited.clear();
  if (!validate(T, R, /*Quiet=*/true) || !validate(Fsucc, R, /*Quiet=*/true))
    return std::nullopt;
  for (BasicBlock *BB : CycleBoundaryBlocks) {
    auto *BBr = dyn_cast<CondBrInst>(BB->getTerminator());
    bool Tolerable = BBr && CycleBoundaryEnd.lookup(BB) == R &&
                      (BBr->getSuccessor(0) == R || BBr->getSuccessor(1) == R);
    if (!Tolerable)
      return std::nullopt;
  }
  ResolvedBlocks.insert(LastRegionVisited.begin(), LastRegionVisited.end());

  // Mirrors `flatten`'s own divergent-branch case exactly (see its
  // comment) -- `TPred`/`FPred` identified by dominance before either arm
  // is mutated.
  auto PredIt = pred_begin(R);
  BasicBlock *Pred0 = *PredIt++;
  BasicBlock *Pred1 = *PredIt;
  BasicBlock *TPred = DT.dominates(T, Pred0) ? Pred0 : Pred1;
  BasicBlock *FPred = TPred == Pred0 ? Pred1 : Pred0;

  Value *Cond = Br->getCondition();
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
  MaskPair TMasks{CondBuilder.CreateAnd(Masks.Live, Cond, "live.t"),
                  CondBuilder.CreateAnd(Masks.SideEffect, Cond, "sideeffect.t")};
  MaskPair FMasks{CondBuilder.CreateAnd(Masks.Live, NotCond, "live.f"),
                  CondBuilder.CreateAnd(Masks.SideEffect, NotCond, "sideeffect.f")};

  UncondBrInst::Create(T, Br->getIterator());
  Br->eraseFromParent();

  // Each arm's own exit `MaskPair` is intentionally discarded here: unlike
  // `flatten`'s own divergent-branch case, this method's only caller
  // (`LoopLinearizer::flattenTaintedBranchesInRegion`) already ran
  // `applyStageMasks` over this whole region with one fixed `Masks` value
  // *before* this call, and is not currently set up to propagate a
  // narrower merged mask back out to the rest of its own region walk --
  // see this method's own doc comment for why that is a known, accepted,
  // scoped limitation for now (safe only because none of this method's
  // current callers' shapes have a `feme.stage.discard`/`.demote` inside
  // such a header-level diamond; a future shape that does would need this
  // revisited).
  flatten(T, R, TMasks, /*RedirectTo=*/Fsucc);
  flatten(Fsucc, R, FMasks, /*RedirectTo=*/R);
  return R;
}

MaskPair DiamondFlattener::flatten(BasicBlock *Cur, BasicBlock *End,
                                   MaskPair Masks, BasicBlock *RedirectTo) {
  for (;;) {
    // Debug aid: with `FEME_DEBUG_FLATTEN_TRACE` set in the environment,
    // print every block this walk's own `for (;;)` loop -- including
    // every nested recursive call this makes for a diamond's own arms --
    // visits as `Cur`, along with that call's own `End`/`RedirectTo` and
    // the owning `DiamondFlattener`'s own `this` pointer (to distinguish
    // genuinely different instances from heap-address reuse across
    // short-lived ones). Added while root-causing roadmap `L375`'s 6th
    // investigation: pinpointed the exact block (and the exact two,
    // structurally-independent paths reaching it) a predecessor-count
    // assertion was silently tripping over, by diffing this trace against
    // itself across the scan-driver-exclusion hypothesis vs. unmodified
    // baseline code.
    if (::getenv("FEME_DEBUG_FLATTEN_TRACE"))
      errs() << "FLATTENTRACE: Cur=" << Cur->getName()
             << " End=" << (End ? End->getName() : "<null>")
             << " RedirectTo="
             << (RedirectTo ? RedirectTo->getName() : "<null>")
             << " this=" << (void *)this << "\n";
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
        (isLoopControlOrHeaderRelayEdge(Cur, Br->getSuccessor(0)) ||
         isLoopControlOrHeaderRelayEdge(Cur, Br->getSuccessor(1)))) {
      // Roadmap H120: this exact cycle boundary block can be reached (and
      // hence recorded here) by more than one distinct `flatten` walk --
      // e.g. once as part of an *enclosing* uniform diamond's own
      // recursive arm walk (whose `validate` call stopped early right
      // here, at a nested cycle, without ever confirming that arm reaches
      // its own reconvergence point -- see `validate`'s identical early
      // return just above), and again, separately, when that nested
      // cycle's own exit block is later flattened as its own root (see
      // `run`) and that walk's continuation happens to reach this same
      // outer boundary block a second time via a single, incomplete path
      // (one arm only) rather than the enclosing diamond's own properly
      // two-arm-merged mask. Whichever walk reaches `Cur` *first* -- by
      // `run`'s own root-processing order, always the enclosing region's
      // complete, every-arm-merged walk, since a nested cycle's own exit
      // root is only ever discovered (and so only ever queued) *after*
      // the walk that reaches it as a boundary in the first place -- is
      // the correct, authoritative one; a later walk reaching the same
      // `Cur` through only one of several structural paths must not
      // clobber it, or a genuine two-predecessor merge like this one can
      // end up seeded with a mask that provably does not dominate one of
      // `Cur`'s own real successors (this row's own crash: an
      // `InstCombine` "Dominance relation broken?" assertion on exactly
      // this shape, confirmed via temporary tracing that the second,
      // narrower recording was silently overwriting the first, correct
      // one here). Return whichever `MaskPair` is now authoritative for
      // `Cur` (the freshly-inserted one, or the pre-existing one this
      // walk must defer to) rather than this walk's own possibly-stale
      // local `Masks`, so a caller using this return value (e.g. as one
      // arm's `TExit`/`FExit` of an *enclosing* diamond) never observes a
      // different answer than a second walk reaching the same `Cur` would.
      //
      // Roadmap K6z: mirrors `validate`'s own identical check -- when
      // `Cur` is a nested sibling cycle `LoopLinearizer`'s post-order
      // already fully linearized before this call, do not stop here at
      // all. Its own already-finalized interior is left completely
      // untouched (never masked, never walked into); only its *entry* is
      // retroactively narrowed by the mask this walk has accumulated so
      // far (the same narrowing `LoopLinearizer::rethreadNestedEntryMasks`
      // already applies wherever an enclosing cycle's own header/latch/
      // exit reaches a previously linearized child), and the walk simply
      // continues from that cycle's one exit block onward to `End`,
      // exactly as `validate` already approved.
      if (GetAlreadyLinearizedCycleExit) {
        if (BasicBlock *ExitBB = GetAlreadyLinearizedCycleExit(Cur)) {
          if (NarrowChildEntryMasks)
            NarrowChildEntryMasks(Cur, Masks);
          Cur = ExitBB;
          continue;
        }
      }
      return CycleBoundaryMasks.try_emplace(Cur, Masks).first->second.get();
    }

    // Roadmap L447: mirrors `validate`'s own identical new check above --
    // see its comment for the full rationale, and `IsWholeFunctionPass`'s
    // own comment for why this is gated on it (only `run()`'s own
    // whole-function walk defers; `flattenLoopBodyDiamond`'s/
    // `flattenLoopBodyRegion`'s own recursive `flatten` calls through
    // this same method must not). Must stay in exact lockstep with
    // whatever `validate` already approved: that walk treats this
    // exact shape (a divergent branch inside a cycle, not the cycle's own
    // control edge, whose diamond contains a mask op) as a no-op
    // pass-through from `Cur` straight to its reconvergence point, so
    // this walk must do the same -- leaving the diamond itself (and
    // anything inside it `applyStageMasks` would otherwise touch)
    // completely untouched for `LoopLinearizer::linearizeCycle`'s own
    // `flattenLoopBodyDiamond` fixed-point loop to pick up later, once
    // the cycle's real per-iteration mask is known.
    if (auto *DeferBr = dyn_cast<CondBrInst>(Term);
        IsWholeFunctionPass && DeferBr && isInCycle(Cur)) {
      if (BasicBlock *R = immediatePostDom(Cur)) {
        if (regionHasStageMaskOps(Cur, R)) {
          Cur = R;
          continue;
        }
      }
    }

    applyStageMasks(*Cur, Masks, &MaskedLoadResults);

    if (isa<ReturnInst>(Term))
      return Masks; // Only reachable at the outermost call (End == nullptr).

    if (auto *UBr = dyn_cast<UncondBrInst>(Term)) {
      BasicBlock *Succ = UBr->getSuccessor(0);
      if (Succ == End) {
        if (RedirectTo != End) {
          UBr->setSuccessor(0, RedirectTo);
        }
        return Masks;
      }
      Cur = Succ;
      continue;
    }

    auto *Br = cast<CondBrInst>(Term);
    BasicBlock *T = Br->getSuccessor(0);
    BasicBlock *Fsucc = Br->getSuccessor(1);

    BasicBlock *R = immediatePostDom(Cur);

    // Roadmap H75: `UI` was computed once, before this walk masked
    // anything (see `feme::cpu::LinearizePass::run`), so it cannot know a
    // masked load's result (see `applyStageMasks`'s own `MaskedLoads`
    // comment) is now per-lane-varying even when the memory it reads is
    // itself perfectly uniform -- a masked-off lane reads the passthru
    // (zero) instead. A branch built from one, left on the "uniform" path
    // below, would keep its real `br`/`phi` shape with every lane
    // executing it unconditionally (see the comment there), reintroducing
    // exactly the divergent branch this pass exists to remove -- found
    // reducing `dEQP-VK.mesh_shader.ext.misc.barrier_in_mesh`/
    // `barrier_in_task` down to a single-invocation-gated `if (counter ==
    // 32) {...} else {...}` reading a barrier-synchronized shared counter.
    SmallPtrSet<Value *, 8> Visited;
    bool CondTainted =
        dependsOnTaintedValue(Br->getCondition(), MaskedLoadResults, Visited);

    if (!UI.isDivergentTerminator(Br) && !CondTainted) {
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
  // Roadmap L432(b): every block this whole-function validation phase
  // below visits is recorded here (via `LastRegionVisited`, the same
  // tracking `flattenLoopBodyDiamond`'s own rollback already relies on --
  // see its comment) so it can be rolled back out of the *shared*
  // `MergeVisited` set if this phase ends up deciding not to flatten
  // anything at all. Without this, a function with no actual divergent
  // branch (or one whose `validate` outright fails) still leaves every
  // block this phase merely looked at permanently claimed in
  // `MergeVisited`, with no matching `ResolvedBlocks` entry -- exactly
  // the `L375` "reached by more than one divergent root" false positive,
  // now firing against `LoopLinearizer`'s own, later, legitimate walk
  // over the very same blocks instead of another `DiamondFlattener` call.
  // Found reproducing `dEQP-VK.synchronization.op.*.write_blit_image_
  // read_image_compute.image_128_r32_uint_concurrent`'s own uniform loop:
  // this whole-function pass's entry-root walk passes straight through
  // the loop's header, body, and latch chain (merely to discover the
  // loop's own cycle-boundary exit, never to flatten anything inside it,
  // since the loop's exit check is uniform, not a divergent diamond) and,
  // finding no divergent branch anywhere else in the function either,
  // bails out below with every one of those blocks still claimed.
  LastRegionVisited.clear();
  SmallVector<BasicBlock *, 4> Roots{&F.getEntryBlock()};
  SmallPtrSet<BasicBlock *, 8> Considered{&F.getEntryBlock()};
  for (unsigned I = 0; I != Roots.size(); ++I) {
    CycleBoundaryBlocks.clear();
    // Roadmap L151: a cycle-exit root discovered below by an *earlier*
    // iteration already has its own `RootEnd` entry (see below) recorded
    // before that iteration ends -- use it here too so this root's own
    // validation does not needlessly re-walk (and, more importantly,
    // does not itself have to reason about) code past whatever enclosing
    // diamond's own arm this cycle was nested in; the entry root itself,
    // and any cycle-exit root not nested in an enclosing diamond's arm,
    // simply get `nullptr` back (this map's default), unchanged from
    // before this map existed.
    if (!validate(Roots[I], RootEnd.lookup(Roots[I]))) {
      // Roadmap L432(b): see the rollback comment above this loop.
      for (BasicBlock *BB : LastRegionVisited)
        MergeVisited.erase(BB);
      return false;
    }
    for (BasicBlock *CycleBlock : CycleBoundaryBlocks) {
      SmallVector<BasicBlock *, 2> Exits;
      CI.getExitBlocks(CI.getCycle(CycleBlock), Exits);
      for (BasicBlock *Exit : Exits) {
        // Roadmap H95a: remember which boundary block(s) feed this exit
        // root, so the mutation phase below can look up the real mask(s)
        // `flatten` records for them (see `CycleBoundaryMasks`) instead of
        // always assuming every lane reaches it.
        ExitToBoundaryBlocks[Exit].push_back(CycleBlock);
        if (Considered.insert(Exit).second) {
          Roots.push_back(Exit);
          // Roadmap L151: seed this new root's own `End` (see `RootEnd`)
          // from whichever walk recorded `CycleBlock` first -- consistent
          // with `CycleBoundaryMasks`'s own "first walk is authoritative"
          // rule for the exact same kind of boundary-block ambiguity.
          RootEnd.try_emplace(Exit, CycleBoundaryEnd.lookup(CycleBlock));
        }
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
    if (isInCycle(&BB) &&
        (isLoopControlOrHeaderRelayEdge(&BB, Br->getSuccessor(0)) ||
         isLoopControlOrHeaderRelayEdge(&BB, Br->getSuccessor(1))))
      continue; // The loop's own iteration decision, not a diamond.
    HasDivergentBranch = true;
    break;
  }
  if (!HasDivergentBranch && !hasStageMaskOps(F) &&
      feme::getShaderStage(F) != feme::ShaderStage::Fragment) {
    // Roadmap L432(b): see the rollback comment above the validation loop
    // -- this phase is about to decline the whole function, so undo every
    // claim it made on `MergeVisited` along the way.
    for (BasicBlock *BB : LastRegionVisited)
      MergeVisited.erase(BB);
    return false;
  }

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
        // Roadmap L440: a found-but-invalid (nulled) entry means the
        // instruction(s) it referred to have since been erased by this
        // same walk's own processing of an earlier root -- treat it
        // identically to "never recorded" rather than reading a stale
        // value back.
        if (MaskIt == CycleBoundaryMasks.end() || !MaskIt->second.isValid())
          continue;
        if (!Seen) {
          Candidate = MaskIt->second.get();
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
    // Roadmap L151: stop this root's own walk at whichever reconvergence
    // point (if any) `RootEnd` recorded for it, redirecting the edge that
    // would otherwise land there right back at itself (a no-op redirect;
    // see `flatten`'s own `RedirectTo` comment) -- i.e. reach it and
    // return without touching it or anything past it, leaving that
    // entirely to whichever *other* root's own walk owns the enclosing
    // diamond this cycle was nested in. `nullptr` (this map's default for
    // the entry root, and any cycle-exit root not nested in an enclosing
    // diamond's arm) preserves this call's pre-existing "walk all the way
    // to a `ret`" behavior exactly.
    BasicBlock *End = RootEnd.lookup(Root);
    flatten(Root, End, EntryMasks, /*RedirectTo=*/End);
  }
  // Roadmap L433: this whole-function walk just committed every root above
  // (mutating the IR for real, unlike the declined-and-rolled-back paths
  // above that `return false` instead) -- every block its own validation
  // phase visited (`LastRegionVisited`, the exact same tracking the
  // declined paths already roll back out of `MergeVisited`) must be
  // released into `ResolvedBlocks` too, exactly as `flattenLoopBodyDiamond`/
  // `flattenLoopBodyRegion`'s own successful commits already do (see
  // `ResolvedBlocks`'s own comment) -- otherwise a block this walk merely
  // passed through on its way to a cycle boundary (never itself flattened,
  // e.g. a uniform loop's own header/exit-check chain, left completely
  // untouched since the loop's exit check is not a divergent diamond) stays
  // claimed in `MergeVisited` with no matching release, so the very next,
  // entirely legitimate walk over those same blocks (`LoopLinearizer`'s own
  // later `flattenLoopBodyRegion` call for that same loop) spuriously hits
  // the `L375` "reached by more than one divergent root" diagnostic.
  // Reproduced by `dEQP-VK.synchronization.op.single_queue.fence.
  // write_image_tess_eval_read_image_tess_control.image_128_r32_uint`: its
  // domain-stage `main` has a mask-affecting `feme.stage.output.store.f32`
  // call *before* its own uniform image-copy loop, so this pass's
  // `!HasDivergentBranch && !hasStageMaskOps(F)` decline-and-roll-back
  // guard above does not apply (there is a mask op to lower) and this
  // commit path runs instead -- but it walked straight through the loop's
  // header/check chain first, discovering (not flattening) the loop's own
  // cycle boundary, with nothing to roll that back out of `MergeVisited`
  // until now.
  ResolvedBlocks.insert(LastRegionVisited.begin(), LastRegionVisited.end());
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
  LoopLinearizer(
      Function &F, DominatorTree &DT, PostDominatorTree &PDT, CycleInfo &CI,
      UniformityInfo &UI,
      const DenseMap<BasicBlock *, WeakMaskPair> &DiamondFlattenedEntryMasks,
      SmallPtrSetImpl<BasicBlock *> &MergeVisited,
      SmallPtrSetImpl<BasicBlock *> &ResolvedBlocks)
      : F(F), DT(DT), PDT(PDT), CI(CI), MergeVisited(MergeVisited),
        ResolvedBlocks(ResolvedBlocks), UI(UI),
        DiamondFlattenedEntryMasks(DiamondFlattenedEntryMasks) {}

  /// Validates and linearizes every leaf cycle in \p F matching the shape
  /// this pass supports. Returns whether \p F was changed.
  bool run();

private:
  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  CycleInfo &CI;

  /// Roadmap L375 (6th investigation): see `DiamondFlattener::MergeVisited`
  /// for the full rationale -- the exact same `SmallPtrSet`
  /// `LinearizePass::run`'s own per-`Function` driver owns, threaded in by
  /// reference here so every `DiamondFlattener` this class constructs
  /// internally (for a single cycle's scan-driver candidates, or for one
  /// `flattenLoopBodyRegion` call) shares it too, rather than each getting
  /// its own, empty, unaware-of-the-others set.
  SmallPtrSetImpl<BasicBlock *> &MergeVisited;

  /// Roadmap L425: see `DiamondFlattener::ResolvedBlocks` for the full
  /// rationale -- threaded through here the same way `MergeVisited` is
  /// above, so every `DiamondFlattener` this class constructs shares one,
  /// whole-`Function` record of which blocks are already safely resolved.
  SmallPtrSetImpl<BasicBlock *> &ResolvedBlocks;

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

  /// Roadmap L196: values this pass itself has synthesized and separately
  /// proven provably uniform "by construction" -- populated exclusively
  /// by `markUniformIfOperandsAreUniform`/`createUniformMaskAny` at each
  /// point in `linearizeCycle`/`closeLatch` that builds one, never by
  /// consulting `UI`. This is the mechanism roadmap L188's own root-cause
  /// investigation concluded a real nested-cycle fix would need: `UI` (see
  /// this class's own member comment above) is computed once, before any
  /// cycle in \p F is linearized, and deliberately never recomputed --
  /// meaning a value this pass creates while linearizing one cycle (a
  /// mask-any reduction, a closed-latch continue condition, ...) did not
  /// exist when `UI` was computed and so cannot be soundly asked about via
  /// `UI` directly. `UniformityInfo::isDivergentAtDef`'s own documented
  /// "value not present at analysis time -> conservatively divergent"
  /// default (see `GenericUniformityImpl.h`'s own `isDivergent` comment)
  /// is *safe* to fall through to for such a value (it only ever costs
  /// precision, e.g. this milestone's own classification treating a
  /// genuinely uniform new value as an unsupported second divergent check
  /// instead of recognizing it as a harmless pass-through), but
  /// `UniformityInfo::isDivergentTerminator`'s own block-keyed cache is
  /// not merely imprecise but outright *unsound* to consult on a block
  /// whose original terminator (whatever `UI` actually analyzed) this
  /// pass has since replaced with a brand new one of its own construction
  /// -- the cache reflects the old, erased terminator's own verdict, not
  /// the new one's, and can therefore give a wrong answer in either
  /// direction (see `isDivergentBranch`'s own comment for the concrete
  /// double-so-far-unrealized failure mode this would otherwise permit
  /// once a future session lets `run()` linearize a non-leaf cycle after
  /// its own children). Every place in this class that used to query `UI`
  /// directly for a branch/value this pass itself might have replaced now
  /// goes through `isDivergentBranch`/`isDivergentValue` instead, which
  /// consult this set first.
  SmallPtrSet<const Value *, 32> KnownUniformValues;

  /// Whether \p V is provably uniform "by construction": either a
  /// `Constant` (trivially so -- every lane agrees on a compile-time
  /// constant), or a value this pass itself already recorded in
  /// `KnownUniformValues`. Does *not* fall back to `UI` at all -- callers
  /// needing the full, sound "is `V` divergent" answer (pre-existing IR
  /// included) should call `isDivergentValue` instead; this helper alone
  /// only ever answers "yes, provably uniform" or "not proven either way
  /// by this pass's own construction," never "provably divergent."
  bool isKnownUniform(const Value *V) const;

  /// The sound, `KnownUniformValues`-aware replacement for a raw
  /// `UI.isDivergentAtDef(V)` query anywhere in this pass: returns
  /// `false` (uniform) whenever `isKnownUniform(V)` already proves it so,
  /// otherwise falls back to `UI.isDivergentAtDef(V)` -- correct for any
  /// value that predates this pass's own mutations (exactly what `UI` was
  /// computed against), and safely (if conservatively) divergent for a new
  /// value this pass created but has not (or cannot) prove uniform.
  bool isDivergentValue(const Value *V) const;

  /// The sound, `KnownUniformValues`-aware replacement for a raw
  /// `UI.isDivergentTerminator(Br)` query anywhere in this pass. Unlike
  /// that call -- keyed on `Br`'s own *block*, and therefore stale the
  /// moment this pass replaces a block's terminator with a new one of its
  /// own (see `KnownUniformValues`'s own comment) -- this is keyed on
  /// `Br`'s own *current* condition value via `isDivergentValue`, so it
  /// stays correct no matter how many times this pass itself has already
  /// replaced that block's terminator. Concretely, the failure mode this
  /// avoids: if `Br`'s block's *original* terminator (whatever `UI`
  /// actually analyzed) happened to be genuinely divergent, but this pass
  /// has since replaced it with a brand new, provably-uniform-by-
  /// construction backedge condition (exactly `closeLatch`'s own
  /// product), a raw `UI.isDivergentTerminator(Br)` call would still
  /// report "divergent" (the block-keyed cache never learns of the
  /// replacement) -- misclassifying an already-linearized child cycle's
  /// own harmless, fully-masked continuation branch as a second,
  /// unsupported divergent check the moment a parent cycle's own
  /// `OtherCondBrBlocks` scan reaches it.
  bool isDivergentBranch(const CondBrInst *Br) const;

  /// (Roadmap L419(w)) `collectUniformPassThroughRegion`'s own `CondBr`
  /// classification (`isDivergentBranch` above) only sees `UniformityInfo`
  /// computed once, before this very `linearizeCycle` call's own
  /// `applyStageMasks` narrows any resource/image load's mask operand
  /// inside the region it just accepted -- a `CondBr` that looks uniform
  /// *now* only because it depends on such a load's result, but whose
  /// mask is still a pre-lowering constant `true` at the time
  /// `collectUniformPassThroughRegion` ran, is exactly what
  /// `isMaskDependentLoadResult`/`dependsOnTaintedValue` already know to
  /// distrust -- just too late for `collectUniformPassThroughRegion`'s own
  /// one-time verdict. Rather than rejecting such a region outright (no
  /// fallback existed for that -- see Roadmap `L419(v)`'s own writeup of
  /// the regression that caused), this instead re-examines every block in
  /// \p Region *after* \p Masks has already been applied to it (so any
  /// such load's mask operand is now genuinely narrowed), and physically
  /// flattens -- via `DiamondFlattener::flattenLoopBodyRegion`, the same
  /// taint-aware mechanism the `HeaderDivergent`/`LatchDivergent`
  /// "`BodyRegionStart`" branch of `linearizeCycle` already relies on --
  /// any block whose `CondBr` has become tainted this way. Diagnoses and
  /// returns `false` if a tainted block's own diamond does not have the
  /// simple, single-reconvergence shape `flattenLoopBodyRegion` requires
  /// (should not happen for the shapes this milestone supports, but see
  /// that function's own comment for the precise precondition). Returns
  /// `true` on success (whether or not any block actually needed
  /// flattening). Found reducing `select_iteration_count_
  /// {fragment,vertex}`'s own `do_while_dynamic`/`for_dynamic`/
  /// `while_dynamic_iterations` residual.
  ///
  /// \p C is the cycle currently being linearized (the one `Region`'s
  /// blocks belong to); see this method's own definition comment for why
  /// a candidate whose innermost cycle is some *other*, nested cycle must
  /// be skipped here rather than re-examined as if it belonged to \p C.
  bool flattenTaintedBranchesInRegion(const SmallPtrSetImpl<BasicBlock *> &Region,
                                      MaskPair Masks, CycleRef C);

  /// Returns whether a value has the `.linearized` suffix, indicating it was
  /// created by DiamondFlattener when collapsing a divergent branch. Such
  /// values represent the result of linearizing formerly-divergent decisions
  /// and should be treated specially regardless of UniformityInfo classification.
  static bool isLinearizedValueName(const Value *V) {
    return V->getName().contains(".linearized");
  }

  /// Records \p V (freshly created by this pass) into `KnownUniformValues`
  /// iff every one of \p Ops is *itself* already provably uniform (via
  /// `isDivergentValue`, so an operand may be either a pre-existing value
  /// `UI` already vouches for, or a value this same mechanism already
  /// proved uniform earlier in the same walk) -- a boolean AND/OR/NOT (or
  /// any other side-effect-free combination) of only uniform operands is
  /// itself always uniform, since every active lane necessarily computes
  /// the identical result from identical inputs. Returns \p V unchanged,
  /// for convenient chaining at each call site.
  Value *markUniformIfOperandsAreUniform(Value *V, ArrayRef<Value *> Ops);

  /// `feme::cpu::createMaskAny` is *always* uniform regardless of its own
  /// operand's uniformity -- it models a genuine wave-wide reduction
  /// primitive (every lane observes the same reduced answer, by
  /// definition), not an ordinary per-lane computation -- so this thin
  /// wrapper marks its result uniform unconditionally rather than needing
  /// `markUniformIfOperandsAreUniform`'s own per-operand check.
  Value *createUniformMaskAny(IRBuilderBase &B, Value *Mask,
                              const Twine &Name);

  /// The exit-check shape a single loop block can have: a conditional
  /// branch where exactly one successor is the loop's shared exit block and
  /// the other stays inside the loop.
  struct ExitCheck {
    CondBrInst *Br = nullptr;
    Value *Cond = nullptr;
    BasicBlock *StayInLoop = nullptr;
    bool ExitOnTrue = false;
    // The block whose own terminator directly reaches `ExitBlock`: `BB`
    // itself for a direct match, or -- for a relay match (see
    // `matchExitCheckWithRelay`) -- the relay chain's real *last* hop,
    // i.e. the actual literal CFG predecessor of `ExitBlock`, not its
    // first hop (`uniformRelayChain`'s own return value already resolves
    // this; roadmap L189 fixed a pre-existing bug here where a
    // multi-hop chain's *first* hop was used instead -- see its own
    // comment). Roadmap H94a: this is the block whose own incoming
    // contribution to any of `ExitBlock`'s *other* phis (besides the
    // live/side-effect masks, which `addLatchIncoming` handles
    // separately) is the semantically correct value to also carry along
    // the new `Latch`->`ExitBlock` edge this milestone's "never really
    // exit here, defer to Latch" strategy installs -- see
    // `linearizeCycle`'s own use of it. A `PHINode`'s incoming-block list
    // only ever names a value's true *immediate* predecessor, which is
    // why the last (not first) hop is required whenever the chain has
    // more than one block.
    BasicBlock *RelayBlock = nullptr;
    // Roadmap L419(ac): every relay-chain hop strictly between `Br`'s own
    // exit successor and `ExitBlock` itself (`RelayBlock` included, as
    // the chain's own last entry), in chain order, *only* ever populated
    // by `recoverRelayExitCheck`'s own multi-hop recovery (a direct
    // `matchExitCheck` match leaves this empty, since there is no chain
    // to speak of -- `Br`'s own successor already *is* `ExitBlock`).
    // Needed because a relay hop can itself own real, multi-valued
    // `PHINode`s (not just the trivial pure-relay/constant-dispatch
    // `CondBr`s `recoverRelayExitCheck` tolerates chasing *through*) --
    // seeded, in this milestone's own `loop_in_switch` shape, with the
    // loop's own genuinely runtime-varying carried value, captured by
    // the exit edge specifically (as opposed to the loop's `continue`
    // edge, which a *different*, remaining predecessor of that same hop
    // supplies). Erasing `Br`'s own exit edge --this function's own
    // caller always does, once a `HeaderExit`/`LatchExit` match is
    // accepted -- orphans that predecessor, collapsing any such hop's
    // phi down to its one remaining (`continue`-edge) value and silently
    // discarding the correct, already-exited value for every lane that
    // took this exit edge instead: exactly the root cause this
    // milestone's own `freezeRelayChainExitValues` fixes, using this
    // field to find every phi needing the fix before the edge is gone.
    SmallVector<BasicBlock *, 4> RelayChain;
  };

  /// Recognizes \p BB's terminator as an `ExitCheck` targeting \p ExitBlock,
  /// or `std::nullopt` if it isn't a conditional branch to/from it at all.
  std::optional<ExitCheck> matchExitCheck(BasicBlock &BB,
                                          BasicBlock *ExitBlock);

  /// Roadmap H19k: like `matchExitCheck`, but additionally tries each of
  /// \p BB's own two successors as a candidate "exit" arm reaching \p
  /// ExitBlock through a relay chain (see `uniformRelayChain`) when
  /// neither successor literally *is* \p ExitBlock -- `BreakCriticalEdges`'s
  /// own relay trampoline is the common real-world case left behind once
  /// `foldRedundantFlowBlock`/`peelConstantFlowPredecessors` bypass a
  /// redundant re-derivation. Roadmap L189: the relay chain may itself
  /// pass through a further, genuinely uniform conditional branch on the
  /// way to \p ExitBlock, not just a plain unconditional chain -- see
  /// `uniformRelayChain`'s own comment for why this is sound. (This
  /// milestone was investigated while root-causing roadmap L188, but
  /// does *not* fix it: L188's own real shape never reaches this code at
  /// all, because its divergent exit check belongs to a *non-leaf*
  /// cycle, which `LoopLinearizer::run` skips entirely before any of
  /// this file's per-cycle logic runs -- see L188's own, corrected
  /// roadmap entry.)
  ///
  /// Roadmap L419(h): \p StopBlock, when non-null, is forwarded straight
  /// to `uniformRelayChain` -- see that parameter's own comment there.
  /// Every pre-existing call site leaves it `nullptr` (its own long-
  /// standing behavior, unchanged); only the new `RealExitCandidates`
  /// widening (the one caller that must distinguish a genuine second
  /// exit from an ordinary internal diamond that merely funnels forward
  /// into the cycle's own real exit check) supplies the cycle's `Latch`.
  std::optional<ExitCheck> matchExitCheckWithRelay(BasicBlock &BB,
                                                   BasicBlock *ExitBlock,
                                                   BasicBlock *StopBlock = nullptr);

  /// Roadmap L419(h): whether any block in \p C contains a
  /// mask-affecting operation -- a `feme.stage.*` call this pass must
  /// narrow to a per-lane predicate (`Discard`/`Demote`/`IsHelper`/
  /// `OutputStore`/`StreamEmit`/`StreamCut`/`TaskPayloadStore`/
  /// `SetMeshOutputs`/`EmitMeshTasks`), or a plain `feme.cpu.resource.*`/
  /// `feme.cpu.image.*` call whose own trailing mask operand this pass's
  /// `applyStageMasks` must narrow from its as-lowered `true` default
  /// (see `matchResourceCall`/`matchImageCall`). Factored out of two
  /// call sites that each separately need it: the cycle-wide
  /// "no divergence, no mask ops -> leave alone" bail further down in
  /// `linearizeCycle`, and the `RealExitCandidates` widening just above
  /// it (gating whether a uniform `OtherCondBrBlocks` member is even
  /// worth treating as a candidate real exit check at all -- a loop with
  /// no mask-affecting op has no reason to need one).
  bool cycleHasMaskOps(CycleRef C) const;

  /// Roadmap L292: structural fallback for `HeaderExit`/`LatchExit` when
  /// `matchExitCheck` finds nothing because `StructurizeCFG` has routed
  /// both this cycle's real "continue" edge and its real "exit" edge
  /// through a single shared `Flow`-style dispatch block -- neither of \p
  /// BB's two direct successors is literally the cycle's `ExitBlock`.
  /// `matchExitCheckWithRelay` is not safe to use here: its
  /// `uniformRelayChain` walk has no notion of "this is the cycle's own
  /// header/latch", so it can loop back around the *entire* cycle through
  /// \p BB's own backedge and "discover" that *both* successors
  /// eventually reach `ExitBlock`, reporting the match as ambiguous
  /// instead. Recovers structurally instead: in this exact shape, exactly
  /// one of \p BB's two successors is a trivial, single-purpose relay stub
  /// `StructurizeCFG` splits a critical edge into (no real instructions of
  /// its own, \p BB as its only predecessor) that exists solely to let the
  /// shared dispatch block's own `phi`s distinguish "came from the exit
  /// check" from "came from a real loop iteration" -- that stub is the
  /// real exit arm, so the *other* successor is `StayInLoop`. Returns
  /// `std::nullopt` if \p BB's terminator is not a `CondBr`, or if neither
  /// (or both) successors match this shape.
  std::optional<ExitCheck> recoverRelayExitCheck(BasicBlock *BB, CycleRef C,
                                                 BasicBlock *ExitBlock);

  /// Roadmap L432(a): `Header`/`Latch` themselves are required to end in
  /// a `CondBr` for `matchExitCheck`/`recoverRelayExitCheck` to recognize
  /// anything at all -- but a `Header` that merely owns the loop's
  /// induction `phi` can be a genuinely trivial, single-successor
  /// pass-through block itself, with the loop's *real*, uniform trip-count
  /// check sitting one or more pure-relay hops further forward
  /// (`write_blit_image_read_image_compute.image_128_r32_uint_concurrent`'s
  /// own shape: `Header`'s only instruction is the induction `phi`, its
  /// terminator an unconditional branch to the real check block).
  /// Walks forward from \p From through a bounded chain of blocks whose
  /// own terminator is an `UncondBr` (stopping, same as
  /// `recoverRelayExitCheck`'s own `IsTrivialExitStub`, the moment \p
  /// Latch or \p ExitBlock is reached, or the chain leaves \p C, or the
  /// bound is exceeded) and returns the first block found whose
  /// terminator is a `CondBr`, or `nullptr` if none is found before the
  /// walk terminates. The returned block is only a candidate -- still
  /// passed through the ordinary `matchExitCheck`/`recoverRelayExitCheck`
  /// checks by the caller, exactly as `Header`/`Latch` themselves are --
  /// this helper only locates it.
  BasicBlock *findForwardExitCheckBlock(BasicBlock *From, BasicBlock *Latch,
                                        BasicBlock *ExitBlock, CycleRef C);

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

  /// Roadmap L197: attempts every descendant of \p C, post-order (deepest
  /// first), before attempting \p C itself -- see `run()`'s own comment
  /// for why this order, not just leaf cycles, is now safe to attempt.
  bool linearizeCyclePostOrder(CycleRef C);

  /// Roadmap L197: every cycle's own exit-block list, precomputed by
  /// `run()` up front -- before *any* cycle is linearized -- and consulted
  /// by `linearizeCycle` instead of a live `CI.getExitBlocks` call. This
  /// is the `getExitBlocks`-specific counterpart to `UI`'s own member
  /// comment above: `GenericCycleInfo::getExitBlocks` walks the *live*
  /// successor edges of every block in `CI.getBlocks(C)` -- itself a
  /// frozen snapshot of every block that was ever in \p C, taken once at
  /// `CI.compute()` time, not just the ones still alive -- and lazily
  /// memoizes whatever it finds, permanently, the first time it is
  /// called for a given cycle. A not-yet-processed *parent* cycle's own
  /// `getBlocks(C)` snapshot always includes every one of its
  /// already-processed *child* cycles' own blocks too (cycles nest via a
  /// contiguous Euler-tour range -- see `GenericCycleInfo::contains`'s own
  /// comment), some of which `foldRedundantFlowBlocksInCycle`/
  /// `mergeTrivialRelayBlocksInCycle` may have already `eraseFromParent`'d
  /// by the time that parent is finally attempted (post-order, children
  /// first): calling `CI.getExitBlocks` on the parent at that point would
  /// walk `successors()` of one of those now-freed blocks, a genuine
  /// use-after-free -- precisely the same class of dangling-`CycleInfo`
  /// hazard the L40 follow-up fix (see `UI`'s own comment) hit and fixed
  /// for `UniformityInfo`, just via this analysis's own, separate
  /// lazily-memoized cache instead. Precomputing every cycle's exit
  /// blocks here, in one pass over the pristine, wholly unmutated `CI`
  /// before `run()` linearizes anything at all, sidesteps the hazard
  /// entirely: by the time any cycle -- leaf or, once genuine nested-cycle
  /// support lands, a non-leaf parent -- is actually linearized, its own
  /// exit-block answer was already computed and cached, against blocks
  /// every one of which was still alive at the time.
  DenseMap<CycleRef, SmallVector<BasicBlock *, 2>> ExitBlocksByCycle;

  /// Populates `ExitBlocksByCycle` for every cycle in `CI` (not just
  /// leaves -- see that member's own comment for why a future non-leaf
  /// cycle needs this too), called once at the very start of `run()`
  /// before any mutation begins.
  void precomputeExitBlocks();

  ArrayRef<BasicBlock *> getExitBlocks(CycleRef C) const {
    auto It = ExitBlocksByCycle.find(C);
    if (It == ExitBlocksByCycle.end())
      return {};
    return It->second;
  }

  /// Roadmap L282: every cycle's own header, once successfully
  /// linearized, mapped to the `MaskPair` phis `makeActivePNPair` created
  /// for it (see `linearizeCycle`'s own local lambda) -- populated right
  /// before each of that function's two success paths returns `true`.
  /// Consulted by `rethreadNestedEntryMasks` (see its own comment) when an
  /// *enclosing* cycle is linearized afterward (post-order, so a child is
  /// always recorded here before any parent's own `linearizeCycle` call
  /// can consult it) and discovers that one of the blocks it is about to
  /// thread its own freshly computed `MaskPair` through is itself a
  /// previously linearized child's header.
  ///
  /// Roadmap L440: `WeakMaskPair`-valued. `foldNowTriviallyConstantMaskPhis`
  /// (see its own doc comment) can erase a mask phi as collateral damage
  /// of folding some *other*, enclosing cycle's own masks -- its closure
  /// walk is scoped to that enclosing cycle's own `CycleInfo` membership,
  /// which also includes every nested child cycle's blocks, so a child's
  /// own already-recorded `HeaderActiveMasks` entry can be silently
  /// erased well after it was cached here, with nothing at that erasure
  /// site aware this map even holds a reference to it. Confirmed as the
  /// real root cause of a reproduced, ASLR-dependent use-after-free (see
  /// `rethreadNestedEntryMasks`'s own `dyn_cast<PHINode>(Child.Live)`
  /// read of this map's entries): a plain `MaskPair` here is a raw,
  /// untracked `Value *` with no visibility into that later erasure.
  DenseMap<BasicBlock *, WeakMaskPair> HeaderActiveMasks;

  /// Roadmap L286: `feme::cpu::DiamondFlattener`'s own, separately-run,
  /// earlier pass's record of every not-yet-linearized cycle header it
  /// stopped at while flattening some enclosing divergent diamond (see
  /// that class's own `getCycleBoundaryMasks` comment) -- i.e. the mask
  /// this cycle's entry should have been seeded with all along, had
  /// `DiamondFlattener` and `LoopLinearizer` been a single pass instead of
  /// two run back-to-back (`LinearizePass::run` threads this through
  /// explicitly since they are not). Consulted by `makeActivePNPair`
  /// exactly where it would otherwise seed a bare, unconditional `true`
  /// for one of `Header`'s own non-backedge predecessors -- using this
  /// cycle's own `Header` as the lookup key mirrors `HeaderActiveMasks`'s
  /// identical convention (a cycle's mask is always keyed by its header,
  /// never by a specific predecessor edge, since every structured cycle
  /// this pass supports has exactly one such edge in the first place).
  /// Empty whenever `DiamondFlattener` never actually flattened an
  /// enclosing diamond around this cycle (the overwhelmingly common
  /// case), in which case lookups simply miss and this pass's original,
  /// unconditional-`true` behavior is preserved exactly.
  ///
  /// Roadmap L440: `WeakMaskPair`-valued for the same reason as
  /// `DiamondFlattener::CycleBoundaryMasks` -- this cache is read by
  /// `makeActivePNPair` across this whole pass's own cycle-by-cycle
  /// processing, potentially long after some *other*, earlier-processed
  /// cycle's own mutation has erased the instruction(s) a plain `MaskPair`
  /// here would otherwise still (incorrectly) point at.
  const DenseMap<BasicBlock *, WeakMaskPair> &DiamondFlattenedEntryMasks;

  /// Roadmap L282/L283: \p BB was just handed \p Masks by the caller's own
  /// `applyStageMasks(*BB, Masks)` (an enclosing, currently-being-
  /// linearized cycle's own per-iteration "am I still active" mask) --
  /// if \p BB is also a previously linearized child cycle's own header
  /// (per `HeaderActiveMasks`), every edge entering that child's own
  /// `makeActivePNPair` phi from *outside* its own cycle (as opposed to
  /// its own backedge, which must be left untouched -- distinguished
  /// structurally via `CI.getCycle`/`CI.contains`, not by inspecting what
  /// value currently happens to sit on the edge) is retroactively
  /// narrowed by conjoining it with \p Masks itself: when the edge is
  /// still the literal `true` `makeActivePNPair` originally seeded it
  /// with (the only sound default available at child-linearization time:
  /// nothing yet known might mask that entry down further), narrowing is
  /// a bare replacement (an `and` with a literal `true` is just the other
  /// operand, so no new `and` instruction is needed); otherwise (L283: a
  /// *different* enclosing cycle's own linearization -- e.g.
  /// `freezeLoopCarriedValues`'s generic loop-carried-value repair -- can
  /// replace that literal `true` with some other in-region SSA value,
  /// such as its own entry-mask phi, before this function ever gets a
  /// chance to see it) a genuine `and` instruction combining the existing
  /// edge value with \p Masks is built at the end of that edge's own
  /// predecessor block. \p Masks is defined at the enclosing cycle's own
  /// header, which dominates every block this function is ever called on
  /// (a uniform pass-through region reached only through that header),
  /// and therefore also every one of that header's own predecessors
  /// within the region, so this is always a valid def-dominates-use edit
  /// either way.
  ///
  /// Without this, a lane the enclosing cycle's own check already
  /// deactivated (e.g. a per-lane loop trip count of zero, entering the
  /// loop body only because this pass's masked-execution convention
  /// always enters it structurally) still enters the child cycle marked
  /// unconditionally "active," and if the child's own exit condition
  /// itself never independently becomes false for a lane that was never
  /// supposed to be iterating at all (a real, confirmed shape on
  /// `dEQP-VK.glsl.loops.special.*_dynamic_iterations.dowhile_trap` and
  /// its `nested*` siblings -- see the roadmap `L282`/`L283` entries),
  /// that lane's `feme.cpu.mask.any` reduction never reaches "no lanes
  /// active either," hanging the whole wave forever (confirmed via a
  /// live `gdb` attach: 100% CPU pinned in the child cycle's own
  /// vectorized mask-reduction loop, not any kind of deadlock). `L282`'s
  /// own constant-only version of this function left `dowhile_trap`
  /// itself still hanging: by the time the *outer* `for` loop's own
  /// linearization calls this on the inner `do`-`while`'s header, the
  /// inner loop's entry edge had already been rewritten (by the very
  /// same outer linearization's own `freezeLoopCarriedValues` call, just
  /// above this call site) from a literal `true` to that outer loop's own
  /// `active.live2`-style phi -- no longer a `ConstantInt` the old check
  /// could match at all, so the narrowing silently never applied. This
  /// generalization alone is confirmed (via targeted tracing) to widen
  /// the set of edges actually narrowed, but is *not* by itself
  /// sufficient to fix `dowhile_trap`'s own hang -- see roadmap `L283`'s
  /// writeup for why (a separate, still-open issue in how this cycle's
  /// `DivergentCandidates`-non-empty handling interacts with a nested
  /// child cycle's own already-linearized latch/exit block showing up
  /// among `OtherCondBrBlocks`). Kept anyway since it is independently a
  /// strict improvement with no known regression.
  void rethreadNestedEntryMasks(BasicBlock *BB, const MaskPair &Masks) {
    auto It = HeaderActiveMasks.find(BB);
    if (It == HeaderActiveMasks.end())
      return;
    // Roadmap L440: `.get()` reads back whatever `Child.Live`/
    // `.SideEffect` currently track -- possibly `nullptr`, if either has
    // since been erased by, specifically, some *other* enclosing cycle's
    // own `foldNowTriviallyConstantMaskPhis` call sweeping it up as part
    // of its own closure walk (see this map's own comment). `dyn_cast_
    // if_present` below treats that exactly like the pre-existing "folded
    // down to a plain constant, not a phi" case just below it: nothing
    // left here to narrow, because every real use of the erased value was
    // already replaced before it was erased.
    MaskPair Child = It->second.get();
    CycleRef ChildCycle = CI.getCycle(BB);
    auto Narrow = [&](PHINode *PN, Value *NewMask) {
      for (unsigned I = 0, E = PN->getNumIncomingValues(); I != E; ++I) {
        BasicBlock *Pred = PN->getIncomingBlock(I);
        if (ChildCycle.isValid() && CI.contains(ChildCycle, Pred))
          continue; // The child's own backedge: leave it alone.
        Value *Incoming = PN->getIncomingValue(I);
        if (Incoming == NewMask)
          continue;
        if (auto *C = dyn_cast<ConstantInt>(Incoming); C && C->isOne()) {
          PN->setIncomingValue(I, NewMask);
          continue;
        }
        IRBuilder<> B(Pred->getTerminator());
        PN->setIncomingValue(I, B.CreateAnd(Incoming, NewMask));
      }
    };
    // Roadmap L419(w): `Child.Live`/`Child.SideEffect` are no longer
    // guaranteed to still be their original `PHINode`s --
    // `foldNowTriviallyConstantMaskPhis` (see its own comment) may have
    // since folded either one down to a plain constant, for a child
    // cycle whose masking turned out never to need to become
    // non-constant at all. A constant has no "entry edges" of its own
    // left to narrow in the first place (every lane trivially already
    // agrees), so skipping `Narrow` for it is correct, not merely
    // tolerated -- unlike the `cast<PHINode>` this replaces, which
    // unconditionally assumed a phi and crashed once one of these two
    // masks genuinely folded away (found running
    // `dEQP-VK.glsl.loops.special.do_while_uniform_iterations.
    // dowhile_trap_fragment` through the real compiler pipeline).
    if (auto *PN = dyn_cast_if_present<PHINode>(Child.Live))
      Narrow(PN, Masks.Live);
    if (auto *PN = dyn_cast_if_present<PHINode>(Child.SideEffect))
      Narrow(PN, Masks.SideEffect);
  }
};

/// Roadmap L189: walks forward from \p From (inclusive, guaranteed by
/// `matchExitCheckWithRelay`'s own caller to not already be \p ExitBlock)
/// toward \p ExitBlock, tolerating not just a plain unconditional chain
/// (this function's own pre-L189 behavior, under its former name
/// `straightChain`) but also a further, genuinely uniform (per \p UI,
/// not merely one this milestone has chosen to leave unmasked --
/// `isDivergentTerminator` is the same real divergence test
/// `collectUniformPassThroughRegion` above already trusts for its own,
/// structurally similar uniform-region tolerance) conditional branch
/// along the way, exploring both of its successors. A visited block is
/// never re-expanded (a node reached a second time, e.g. via some
/// uniform sub-region's own backedge, is simply treated as already
/// accounted for, not walked again). Deliberately does *not* require a
/// unique predecessor at each
/// hop the way the old `straightChain` did: a block reached this way may
/// well have another, entirely unrelated real predecessor elsewhere in
/// the function too, but that does not matter here -- this function only
/// needs to know where control flow goes *given* it reached this block
/// via `From`'s own uniform decisions, and a block whose own branch is
/// genuinely uniform executes identically for every active lane
/// regardless of how it was reached, so no masking treatment of it is
/// ever required either way.
///
/// Returns the walk's unique block whose own terminator has `ExitBlock`
/// as a literal successor -- the walk's real *last* hop, the
/// semantically correct block for `ExitCheck::RelayBlock` to name (see
/// its own comment) -- or `std::nullopt` if: `ExitBlock` is never
/// reached this way at all, is reached via more than one such block
/// (ambiguous: which one is "live" would then depend on a runtime value
/// this pass has no way to thread through), or a visited block ends in
/// anything but an `UncondBr`/`CondBr` terminator, or a `CondBr` that is
/// itself genuinely divergent (that would be a second, unrelated
/// divergent check this milestone does not support coexisting with the
/// loop's own real one -- `linearizeCycle`'s own `OtherCondBrBlocks`
/// classification already rejects that shape elsewhere, but this walk
/// must not silently paper over it either).
///
/// \p IsDivergentBranch decides whether a conditional branch encountered
/// mid-chain counts as "genuinely divergent" for this purpose; callers
/// should pass `LoopLinearizer::isDivergentBranch` (bound to the caller's
/// own instance) rather than a raw `UniformityInfo::isDivergentTerminator`
/// query, so that a branch this same pass has itself already replaced --
/// e.g. an already-linearized child cycle's own closed-latch continue
/// condition -- is judged by its *current* condition value rather than a
/// stale, block-keyed cache entry left over from before the replacement
/// (see `LoopLinearizer::KnownUniformValues`'s own comment for why a raw
/// `UI` query is not sound here).
std::optional<BasicBlock *>
uniformRelayChain(BasicBlock *From, BasicBlock *ExitBlock,
                  llvm::function_ref<bool(const CondBrInst *)>
                      IsDivergentBranch,
                  BasicBlock *StopBlock = nullptr) {
  SmallPtrSet<BasicBlock *, 8> Visited;
  SmallVector<BasicBlock *, 8> Worklist{From};
  BasicBlock *LastHop = nullptr;
  auto ConsiderSuccessor = [&](BasicBlock *Cur, BasicBlock *Succ) {
    if (Succ != ExitBlock) {
      Worklist.push_back(Succ);
      return true;
    }
    if (LastHop && LastHop != Cur)
      return false; // Ambiguous: reached via more than one block.
    LastHop = Cur;
    return true;
  };
  while (!Worklist.empty()) {
    BasicBlock *Cur = Worklist.pop_back_val();
    if (!Visited.insert(Cur).second)
      continue; // Already visited via another arm, or a uniform backedge.
    // Roadmap L419(h): a relay walk that only reaches `ExitBlock` by
    // first passing through \p StopBlock (the cycle's own `Latch`, when
    // the caller supplies one) has not found a genuine, independent
    // second exit at all -- it has simply rediscovered the loop's own
    // real exit check by walking all the way around the cycle's
    // backedge (see this function's own comment above: this walk has no
    // built-in notion of "this is the cycle's own backedge," so without
    // this guard a uniform internal diamond that merely funnels forward
    // into the loop's one real exit check -- e.g. a `switch`-lowered
    // `continue`/`break` dispatch's own ordinary arms -- looks
    // indistinguishable from a true second exit). Bail as soon as the
    // walk would cross `StopBlock`, rather than letting it resolve to a
    // (false) single `LastHop` further on.
    if (StopBlock && Cur == StopBlock)
      return std::nullopt;
    if (auto *UBr = dyn_cast<UncondBrInst>(Cur->getTerminator())) {
      if (!ConsiderSuccessor(Cur, UBr->getSuccessor(0)))
        return std::nullopt;
      continue;
    }
    auto *CBr = dyn_cast<CondBrInst>(Cur->getTerminator());
    if (!CBr || IsDivergentBranch(CBr))
      return std::nullopt;
    if (!ConsiderSuccessor(Cur, CBr->getSuccessor(0)) ||
        !ConsiderSuccessor(Cur, CBr->getSuccessor(1)))
      return std::nullopt;
  }
  if (!LastHop)
    return std::nullopt; // Never actually reached `ExitBlock` this way.
  return LastHop;
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
///
/// Returns whether every block in the straight-line, single-successor
/// `UncondBr` chain starting at \p From (which may be \p From itself)
/// eventually reaches \p Target, with every hop *strictly before*
/// \p Target itself (\p Target may legitimately carry real computation
/// of its own, exactly like `isLoopControlEdgeThroughRelay`'s own
/// `StopAt` special-case) a genuine `isPureRelayBlock` -- never a block
/// doing real work of its own, like a loop's own latch trip-count
/// re-check (confirmed necessary on `loop-break-structurized.ll`'s own
/// `latch` block, which is `UncondBr`-terminated but computes a real
/// `%inc`/`%loop.cond` first; without this gate,
/// `peelWouldDestroyAncestorReconvergencePoint` below wrongly treated
/// the loop header's own entirely-legitimate, `LoopLinearizer`-recovered
/// exit-check reconvergence as the dangerous nested-diamond-of-diamonds
/// shape it exists to catch). Used by
/// `peelWouldDestroyAncestorReconvergencePoint` below to check whether an
/// ancestor diamond's *other* arm still reaches the same merge block a
/// peel is about to leave with only one real predecessor.
static bool reachesThroughRelayChain(BasicBlock *From, BasicBlock *Target) {
  BasicBlock *Cur = From;
  while (Cur != Target) {
    if (!isPureRelayBlock(Cur))
      return false;
    auto *Br = dyn_cast<UncondBrInst>(Cur->getTerminator());
    if (!Br)
      return false;
    Cur = Br->getSuccessor(0);
  }
  return true;
}

/// Roadmap L452(d): `peelConstantFlowPredecessors` below (and the
/// `mergeTrivialRelayBlocksInCycle` fold that follows it) can leave \p BB
/// with only one real predecessor once every provably-constant one is
/// bypassed, at which point `mergeTrivialRelayBlocksInCycle` fully merges
/// \p BB away. That is always sound for \p BB's own *values* (exactly
/// what `peelConstantFlowPredecessors`'s own `SSAUpdater` reconciliation
/// handles), but if \p BB is currently serving as some *ancestor*
/// `CondBrInst` diamond's own clean, two-predecessor reconvergence point,
/// eliminating it destroys that shape: the ancestor's bypassed arm now
/// reaches one of \p BB's own former successors directly, while its
/// *other* arm's own merged-in diamond independently reaches that same
/// successor too -- exactly the "outer diamond arm directly reaches a
/// nested sibling diamond's own target" shape
/// `DiamondFlattener::validate`'s own `L375`/`L383(a)` commentary already
/// documents as a genuine, deliberately-unsupported limitation. Reduced
/// from a real `dEQP-VK.spirv_assembly.instruction.compute.
/// opcompositeextract.f16arr3` switch-decode-inside-a-loop shape; see
/// `Roadmap.md`'s own `L452(d)` entry for the full writeup.
///
/// Detects this structurally rather than via a (possibly stale mid-fold,
/// see `linearizeCycle`'s own `L198` comment on why `PDT` cannot be
/// trusted here) dominator-tree query: walks back from \p SoleSurvivor
/// (the one predecessor of \p BB that would remain once every candidate
/// in this call's own `PeelIndices` is bypassed) through single-
/// predecessor relay hops to the real `CondBrInst` it ultimately descends
/// from -- mirroring the same backward walk `peelConstantFlowPredecessors`
/// already performs (for a different reason) to compute its own `Origin`
/// -- then checks whether *both* of that block's successors reach \p BB
/// through nothing but further pure relay hops, i.e. whether \p BB is
/// already that block's own private two-arm reconvergence point.
///
/// \p Header and \p Latch (the enclosing cycle's own, from
/// `peelConstantFlowPredecessorsInCycle`'s own caller) are deliberately
/// exempted from being treated as a dangerous `Origin`: a cycle's own
/// header legitimately, by design, reconverges at a `Flow`-style dispatch
/// block the exact same way an ordinary diamond does (confirmed on
/// `loop-break-structurized.ll`'s own early-`break` header, whose
/// `Flow` dispatch block is both the header's own clean two-arm
/// reconvergence point *and* the one `peelConstantFlowPredecessorsInCycle`
/// is specifically, correctly designed to bypass one arm of) --
/// `LoopLinearizer`'s own later `recoverRelayExitCheck` fallback is built
/// to recover exactly that shape, unlike `DiamondFlattener::validate`'s
/// stricter non-cycle-boundary check this guard otherwise protects.
static bool peelWouldDestroyAncestorReconvergencePoint(BasicBlock *BB,
                                                       BasicBlock *SoleSurvivor,
                                                       BasicBlock *Header,
                                                       BasicBlock *Latch) {
  BasicBlock *Origin = SoleSurvivor;
  while (BasicBlock *Unique = Origin->getUniquePredecessor()) {
    if (isa<CondBrInst>(Origin->getTerminator()))
      break;
    Origin = Unique;
  }
  if (Origin == Header || Origin == Latch)
    return false;
  auto *OriginBr = dyn_cast<CondBrInst>(Origin->getTerminator());
  if (!OriginBr)
    return false;
  return reachesThroughRelayChain(OriginBr->getSuccessor(0), BB) &&
         reachesThroughRelayChain(OriginBr->getSuccessor(1), BB);
}

bool peelConstantFlowPredecessors(
    BasicBlock *BB, SmallPtrSetImpl<BasicBlock *> &PeeledFrom,
    SmallPtrSetImpl<BasicBlock *> *RelayTargets = nullptr,
    BasicBlock *CycleHeader = nullptr, BasicBlock *CycleLatch = nullptr) {
  auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
  if (!Br)
    return false;
  bool Negated;
  Instruction *CondUser;
  PHINode *CondPN = getFlowConditionPhi(Br, BB, Negated, CondUser);
  if (!CondPN)
    return false;

  // Roadmap L200: bail out entirely if `BB` contains any non-`phi`
  // instruction whose result escapes `BB` (used by an instruction in some
  // other block). Peeling below redirects one of `BB`'s predecessors to
  // bypass it outright; every one of `BB`'s own `phi`s gets its downstream
  // uses repaired via `SSAUpdater` (see the `Incoming`/`Updater` loop
  // further down), but an *ordinary* instruction defined in `BB` -- not a
  // `phi` -- has no equivalent path-sensitive value this fold could
  // substitute for the newly-bypassed predecessor. Once any predecessor is
  // peeled, `BB` no longer necessarily dominates every place its own
  // instructions are used, and the verifier requires that regardless of
  // which particular (still fully valid, at runtime) path a given use
  // actually observes. Reduced from a real, reliably-reproducing
  // `dEQP-VK.graphicsfuzz.cov-nested-loop-large-array-index-using-vector-
  // components` crash: a `%.inv = xor i1 <phi>, true`-shaped value in `BB`,
  // itself consumed further downstream through more merges, was left used
  // without dominating its use once this fold's own peel of an unrelated
  // predecessor added a second, bypassing edge into one of `BB`'s
  // descendants -- an `Instruction does not dominate all uses!` verifier
  // failure that (with verification disabled, as `feme-cpu-linearize`'s
  // own JIT-embedded pipeline runs) only actually asserted much further
  // downstream, inside `formLCSSAImpl`, once the enclosing loop tried to
  // complete LCSSA form over the now-invalid def/use.
  for (Instruction &I : *BB) {
    if (isa<PHINode>(I) || I.isTerminator())
      continue;
    for (User *U : I.users())
      if (cast<Instruction>(U)->getParent() != BB)
        return false;
  }

  // Collect every incoming index whose value is a literal constant --
  // candidates to peel. If none are, there is nothing to do here; if
  // *every* one is, `foldRedundantFlowBlock` above already fully replaces
  // `BB`, more thoroughly, so leave that shape to it instead.
  SmallVector<unsigned, 2> PeelIndices;
  for (unsigned I = 0, E = CondPN->getNumIncomingValues(); I != E; ++I)
    if (isa<ConstantInt>(CondPN->getIncomingValue(I)))
      PeelIndices.push_back(I);
  if (PeelIndices.empty() ||
      PeelIndices.size() == CondPN->getNumIncomingValues())
    return false;

  // Roadmap L452(d): if peeling every candidate above would leave `BB`
  // with exactly one real predecessor remaining, check whether `BB` is
  // currently serving as some ancestor diamond's own clean reconvergence
  // point before touching anything -- see
  // `peelWouldDestroyAncestorReconvergencePoint`'s own comment for the
  // exact shape this guards against. Bail out entirely (leaving `BB`,
  // and the harmless-but-redundant constant re-derivation it still
  // performs, standing) rather than risk it.
  if (CondPN->getNumIncomingValues() - PeelIndices.size() == 1) {
    unsigned SurvivorIdx = CondPN->getNumIncomingValues();
    for (unsigned Idx = 0, E = CondPN->getNumIncomingValues(); Idx != E;
         ++Idx) {
      if (!llvm::is_contained(PeelIndices, Idx)) {
        SurvivorIdx = Idx;
        break;
      }
    }
    assert(SurvivorIdx != CondPN->getNumIncomingValues() &&
           "exactly one incoming index must survive peeling here");
    BasicBlock *SoleSurvivor = CondPN->getIncomingBlock(SurvivorIdx);
    if (peelWouldDestroyAncestorReconvergencePoint(BB, SoleSurvivor,
                                                   CycleHeader, CycleLatch))
      return false;
  }

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
    BasicBlock *Target =
        ExitOnSucc0 ? Br->getSuccessor(0) : Br->getSuccessor(1);

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

    if (::getenv("FEME_DEBUG_PEEL_TRACE")) {
      errs() << "PEELTRACE: peeling pred=" << Pred->getName()
             << " of BB=" << BB->getName()
             << " into Target=" << Target->getName()
             << " TargetHasPhis=" << !Target->phis().empty() << "\n";
      for (auto &Pair : Incoming)
        errs() << "PEELTRACE:   incoming phi=" << Pair.first->getName()
               << " valueAlongPred=" << *Pair.second << "\n";
    }

    // Roadmap L190: `Target` can *already* have a `phi` reconciling some
    // value across `BB` and an unrelated predecessor -- left behind by an
    // earlier peel that bypassed a *different* predecessor of `BB`
    // directly into `Target` before this one ever ran (that earlier
    // peel's own `SSAUpdater` use-rewriting, below, synthesized it on
    // demand for some genuinely external use). `SSAUpdater::RewriteUse`
    // only ever replaces the value of a *single existing* `Use` in
    // place; given a `Use` that is itself one of that existing `phi`'s
    // incoming operands for `BB`, it has no way to *split* that one slot
    // into two (one for `BB`, one for the new, now-direct edge from
    // `Pred`) -- so left to the use-rewriting loop alone, `Pred` would
    // become a real predecessor of `Target` without any such `phi` ever
    // gaining a matching incoming entry for it. Add that missing entry
    // explicitly, before rewriting any other, genuinely external use of
    // `BB`'s own phis below.
    //
    // The value to use for the new entry is usually the *existing*
    // `BB`-slot value verbatim: intervening folds (`collapseTrivially
    // RedundantPhisInCycle`) only ever collapse a `phi` once every one of
    // its *current* incoming values agree, so whatever a downstream
    // `phi` already holds for `BB` is loop-invariant across `BB`'s
    // predecessors *unless* it is still literally one of `BB`'s own,
    // not-yet-collapsed `phi`s (one of this call's own `Incoming` pairs)
    // -- in which case the value along `Pred` specifically is that
    // pair's own peeled value instead, exactly like every other use of
    // that `phi` being rewritten below.
    for (PHINode &TargetPN : Target->phis()) {
      int Idx = TargetPN.getBasicBlockIndex(BB);
      if (Idx < 0)
        continue;
      Value *NewV = TargetPN.getIncomingValue(Idx);
      for (auto &Pair : Incoming) {
        if (Pair.first == NewV) {
          NewV = Pair.second;
          break;
        }
      }
      TargetPN.addIncoming(NewV, Pred);
    }

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
      //
      // Roadmap L190: "genuinely outside `BB`" means the *user
      // instruction itself* is not physically located in `BB` --
      // `UserInst->getParent() == BB`, checked the same way for a `phi`
      // user as for any other. An earlier version of this loop instead
      // special-cased a `PHINode` user to `UserPN->getIncomingBlock(U)`
      // (the predecessor an incoming value arrives *from*, not the
      // block the `phi` itself lives in), reasoning it needed to mirror
      // `SSAUpdater::RewriteUse`'s own dispatch (which *does* resolve a
      // `phi` operand's reaching value as of the end of its incoming
      // block rather than the middle of the `phi`'s own block) -- but
      // `SSAUpdater::RewriteUse` already performs that dispatch
      // correctly on its own once called; this pre-check exists only to
      // decide whether to call it *at all*, for which the user's own
      // physical location is what matters. That mismatch let a genuinely
      // downstream `phi` slip through unrewritten whenever its incoming
      // value for *this* edge happened to be labeled with `BB` as the
      // source block (always true of any `phi` immediately past `BB`,
      // e.g. `Target`'s own `phi`s reconciling `BB`'s two now-merged
      // predecessors) -- silently leaving that `phi` with one fewer
      // entry than `Target`'s real predecessor count once `Pred` is
      // retargeted below, corrupting the IR (reproduced via
      // `dEQP-VK.graphicsfuzz.complex-nested-loops-and-call`'s own
      // doubly-nested nested-loop shape, where a second, later peel in
      // the same cycle retargets a predecessor of a block whose own
      // condition a first, earlier peel had already collapsed into one
      // of `Target`'s `phi`s).
      for (Use &U : llvm::make_early_inc_range(PN->uses())) {
        auto *UserInst = cast<Instruction>(U.getUser());
        if (UserInst->getParent() == BB)
          continue;
        if (::getenv("FEME_DEBUG_PEEL_TRACE"))
          errs() << "PEELTRACE:   rewriting use of " << PN->getName()
                 << " in " << *UserInst << "\n";
        Updater.RewriteUse(U);
        if (::getenv("FEME_DEBUG_PEEL_TRACE"))
          errs() << "PEELTRACE:   after rewrite: " << *UserInst << "\n";
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
      if (peelConstantFlowPredecessors(&BB, PeeledFrom, RelayTargets, Header,
                                       Latch)) {
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
                                        BasicBlock *ExitBlock,
                                        BasicBlock *StopBlock) {
  if (std::optional<ExitCheck> Direct = matchExitCheck(BB, ExitBlock))
    return Direct;
  auto *Br = dyn_cast<CondBrInst>(BB.getTerminator());
  if (!Br)
    return std::nullopt;

  std::optional<ExitCheck> Result;
  for (unsigned I = 0; I != 2; ++I) {
    BasicBlock *Candidate = Br->getSuccessor(I);
    std::optional<BasicBlock *> Relay = uniformRelayChain(
        Candidate, ExitBlock,
        [this](const CondBrInst *CBr) { return isDivergentBranch(CBr); },
        StopBlock);
    if (!Relay)
      continue;
    if (Result)
      return std::nullopt; // Both arms reach it: ambiguous.
    ExitCheck EC;
    EC.Br = Br;
    EC.Cond = Br->getCondition();
    EC.ExitOnTrue = (I == 0);
    EC.StayInLoop = Br->getSuccessor(1 - I);
    EC.RelayBlock = *Relay;
    Result = EC;
  }
  return Result;
}

bool LoopLinearizer::cycleHasMaskOps(CycleRef C) const {
  for (BasicBlock &BB : F) {
    if (!CI.contains(C, &BB))
      continue;
    for (const Instruction &I : BB) {
      auto *Call = dyn_cast<CallInst>(&I);
      if (!Call)
        continue;
      feme::StageOpKind Kind;
      if (feme::isStageOpCall(*Call, &Kind) &&
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
      if (matchResourceCall(*Call) || matchImageCall(*Call))
        return true;
    }
  }
  return false;
}

std::optional<LoopLinearizer::ExitCheck>
LoopLinearizer::recoverRelayExitCheck(BasicBlock *BB, CycleRef C,
                                      BasicBlock *ExitBlock) {
  auto *Br = dyn_cast<CondBrInst>(BB->getTerminator());
  if (!Br)
    return std::nullopt;
  // Roadmap L294: a trivial-looking relay stub is only a genuine exit arm
  // if it actually, eventually reaches `ExitBlock` -- chasing through
  // further pure, single-successor relay hops (bounded depth, same as
  // `uniformRelayChain`'s own bound) is required rather than trusting the
  // immediate successor alone, since `ifblock`/`elseblock`-shaped loops
  // (`dEQP-VK.glsl.loops.special.for_dynamic_iterations.ifblock_fragment`)
  // route `Header`'s own divergent trip-count recheck through a stub that
  // looks identical to a real exit arm but actually lands back on a
  // *different*, still-in-loop divergent dispatch block shared with the
  // loop's own `break`/`continue` check (`OtherCondBrBlocks`'s own entry),
  // not on `ExitBlock` at all.
  //
  // Roadmap L419(ab): returns the chain's actual *last* hop (the literal
  // CFG predecessor that ultimately reaches `ExitBlock`), not merely
  // whether one exists -- `ExitCheck::RelayBlock`'s own contract (see its
  // comment) requires the last hop, since a `PHINode`'s incoming-block
  // list only ever names a value's true immediate predecessor. Also
  // tolerates one additional shape beyond a pure `UncondBr` relay chain:
  // a `CondBr` whose condition is a `phi` defined in the block being
  // chased, with a known `ConstantInt` incoming value specifically for
  // the edge just walked -- i.e., a shared dispatch block whose own
  // "decision" is really just a relabeling of which of `BB`'s two
  // successors was taken, not a genuinely independent second divergent
  // check. Folding that edge and continuing the chase down whichever
  // successor the constant selects covers the `loop_in_switch`/
  // `elseblock`/`ifblock` shapes, where `Header`'s own divergent
  // trip-count check funnels through exactly such a block before
  // reaching the loop's real continue/exit dispatch.
  // Roadmap L419(ac): \p Chain, when non-null, collects every hop visited
  // (`Succ` itself through the returned last hop, inclusive) in chain
  // order -- see `ExitCheck::RelayChain`'s own comment for why the
  // caller needs this.
  auto IsTrivialExitStub =
      [&](BasicBlock *Succ,
          SmallVectorImpl<BasicBlock *> *Chain) -> BasicBlock * {
    if (!CI.contains(C, Succ) || !isPureRelayBlock(Succ) ||
        Succ->getUniquePredecessor() != BB)
      return nullptr;
    BasicBlock *Cur = Succ;
    BasicBlock *Prev = BB;
    for (unsigned Depth = 0; Depth != 8; ++Depth) {
      if (Cur == ExitBlock)
        return Prev;
      if (Chain)
        Chain->push_back(Cur);
      if (auto *UBr = dyn_cast<UncondBrInst>(Cur->getTerminator())) {
        if (Cur != Succ && !isPureRelayBlock(Cur))
          return nullptr;
        Prev = Cur;
        Cur = UBr->getSuccessor(0);
        continue;
      }
      auto *CondBr = dyn_cast<CondBrInst>(Cur->getTerminator());
      if (!CondBr)
        return nullptr;
      auto *PN = dyn_cast<PHINode>(CondBr->getCondition());
      if (!PN || PN->getParent() != Cur)
        return nullptr;
      int Idx = PN->getBasicBlockIndex(Prev);
      if (Idx == -1)
        return nullptr;
      auto *ConstIncoming = dyn_cast<ConstantInt>(PN->getIncomingValue(Idx));
      if (!ConstIncoming)
        return nullptr;
      Prev = Cur;
      Cur = CondBr->getSuccessor(ConstIncoming->isOne() ? 0 : 1);
    }
    return Cur == ExitBlock ? Prev : nullptr;
  };
  SmallVector<BasicBlock *, 4> Chain0, Chain1;
  BasicBlock *Succ0Relay = IsTrivialExitStub(Br->getSuccessor(0), &Chain0);
  BasicBlock *Succ1Relay = IsTrivialExitStub(Br->getSuccessor(1), &Chain1);
  if ((Succ0Relay != nullptr) == (Succ1Relay != nullptr))
    return std::nullopt; // Neither (or, ambiguously, both) match this shape.
  ExitCheck EC;
  EC.Br = Br;
  EC.Cond = Br->getCondition();
  EC.ExitOnTrue = Succ0Relay != nullptr;
  EC.StayInLoop = Succ0Relay ? Br->getSuccessor(1) : Br->getSuccessor(0);
  EC.RelayBlock = Succ0Relay ? Succ0Relay : Succ1Relay;
  EC.RelayChain = Succ0Relay ? std::move(Chain0) : std::move(Chain1);
  return EC;
}

BasicBlock *LoopLinearizer::findForwardExitCheckBlock(BasicBlock *From,
                                                      BasicBlock *Latch,
                                                      BasicBlock *ExitBlock,
                                                      CycleRef C) {
  BasicBlock *Cur = From;
  for (unsigned Depth = 0; Depth != 8; ++Depth) {
    if (Cur == Latch || Cur == ExitBlock || !CI.contains(C, Cur))
      return nullptr;
    if (isa<CondBrInst>(Cur->getTerminator()))
      return Cur == From ? nullptr : Cur;
    auto *UBr = dyn_cast<UncondBrInst>(Cur->getTerminator());
    if (!UBr)
      return nullptr; // Not a shape this milestone understands.
    Cur = UBr->getSuccessor(0);
  }
  return nullptr;
}

std::optional<SmallPtrSet<BasicBlock *, 8>>
LoopLinearizer::collectUniformPassThroughRegion(
    BasicBlock *From, BasicBlock *To, BasicBlock *ExitBlock, CycleRef C,
    const SmallPtrSetImpl<BasicBlock *> &PeeledFrom) {
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
    if (!PeeledFrom.contains(Cur) && isDivergentBranch(CBr))
      return std::nullopt;
    Worklist.push_back(CBr->getSuccessor(0));
    Worklist.push_back(CBr->getSuccessor(1));
  }
  return Visited;
}

bool LoopLinearizer::isKnownUniform(const Value *V) const {
  return isa<Constant>(V) || KnownUniformValues.contains(V);
}

bool LoopLinearizer::isDivergentValue(const Value *V) const {
  if (isKnownUniform(V))
    return false;
  return UI.isDivergentAtDef(V);
}

bool LoopLinearizer::isDivergentBranch(const CondBrInst *Br) const {
  const Value *Cond = Br->getCondition();
  // Linearized values represent the results of flattening previously-divergent
  // diamonds (created by DiamondFlattener). While UniformityInfo may classify
  // them as uniform after flattening, they should always be recognized as
  // candidates for the chained-branch fix (L377a) because they semantically
  // represent formerly-divergent decisions that the fix is designed to handle.
  if (isLinearizedValueName(Cond))
    return true;
  return isDivergentValue(Cond);
}

bool LoopLinearizer::flattenTaintedBranchesInRegion(
    const SmallPtrSetImpl<BasicBlock *> &Region, MaskPair Masks, CycleRef C) {
  // Collect candidates up front: `Region` is a `SmallPtrSet` with no
  // control-flow order, and flattening one block's diamond can delete
  // its own arms -- iterating a fixed snapshot avoids re-examining (or
  // dereferencing) anything a sibling candidate's own flatten already
  // removed.
  SmallVector<BasicBlock *, 4> Candidates;
  SmallPtrSet<Value *, 1> EmptyTainted;
  for (BasicBlock *BB : Region) {
    auto *CBr = dyn_cast<CondBrInst>(BB->getTerminator());
    if (!CBr)
      continue;
    // Roadmap L419(x): `BB` may be a different, nested child cycle's own
    // header or latch -- `PreRegion`/`PostRegion` collect every block on
    // a uniform pass-through walk from this cycle's `CheckBlock`/
    // `CheckExit` without regard to cycle nesting, and a child cycle's
    // own exit check sitting on that walk is not itself a member of this
    // cycle `C`'s own body in the sense this scan cares about -- it was
    // already linearized (and, if genuinely divergent, already correctly
    // masked) in its own right when that child cycle was processed. Res-
    // canning it here as if it were one of `C`'s own candidates double-
    // counts it: `dependsOnTaintedValue` sees the same taint this cycle's
    // masking threaded into a shared resource/image load and incorrectly
    // treats the child cycle's own, already-resolved, genuinely uniform
    // exit check as newly mask-dependent, which `flattenKnownDivergentBranch`
    // then (correctly, given this faulty premise) refuses to flatten --
    // found reducing `do_while_dynamic_iterations.{dowhile_trap,nested,
    // nested_sequence,nested_tricky_dataflow_{1,2}}_{fragment,vertex}`
    // (`L419(w)`'s own newly-regressed cases). Excluding `BB` here, at
    // the point this scan actually consumes cycle membership, is
    // deliberately narrower than excluding it from `OtherCondBrBlocks`'s
    // own raw collection in `linearizeCycle` -- an earlier attempt at the
    // latter caused an infinite loop (that collection's result is relied
    // on by several other, unrelated decisions this scan does not make).
    if (CycleRef BBCycle = CI.getCycle(BB); BBCycle.isValid() && BBCycle != C)
      continue;
    SmallPtrSet<Value *, 8> Visited;
    if (dependsOnTaintedValue(CBr->getCondition(), EmptyTainted, Visited))
      Candidates.push_back(BB);
  }
  for (BasicBlock *BB : Candidates) {
    // A sibling candidate's own flatten may have already folded this one
    // away as one of its own arms.
    if (!isa<CondBrInst>(BB->getTerminator()))
      continue;
    DiamondFlattener DF(F, DT, PDT, CI, UI, MergeVisited, ResolvedBlocks);
    if (!DF.flattenKnownDivergentBranch(BB, Masks)) {
      diagnose(F, "loop has a branch in '" + BB->getName() +
                      "' that became mask-dependent after masking but "
                      "does not have the simple shape this pass can "
                      "flatten (roadmap milestone 6 deviation)");
      return false;
    }
  }
  return true;
}

Value *LoopLinearizer::markUniformIfOperandsAreUniform(Value *V,
                                                       ArrayRef<Value *> Ops) {
  if (llvm::all_of(Ops,
                   [&](Value *Op) { return !isDivergentValue(Op); }))
    KnownUniformValues.insert(V);
  return V;
}

Value *LoopLinearizer::createUniformMaskAny(IRBuilderBase &B, Value *Mask,
                                           const Twine &Name) {
  Value *V = createMaskAny(B, Mask, Name);
  KnownUniformValues.insert(V);
  return V;
}

Value *LoopLinearizer::closeLatch(BasicBlock *Latch, BasicBlock *Header,
                                  const MaskPair &MasksAtLatch) {
  auto *NaturalBr = dyn_cast<CondBrInst>(Latch->getTerminator());
  IRBuilder<> B(Latch->getTerminator());
  Value *AnyActive = createUniformMaskAny(B, MasksAtLatch.Live, "loop.any.active");
  Value *Continue;
  if (NaturalBr) {
    Value *NaturalCond = NaturalBr->getCondition();
    bool ContinueOnTrue = NaturalBr->getSuccessor(0) == Header;
    Value *NaturalContinue = NaturalCond;
    if (!ContinueOnTrue)
      NaturalContinue = markUniformIfOperandsAreUniform(
          B.CreateNot(NaturalCond), {NaturalCond});
    Continue = markUniformIfOperandsAreUniform(
        B.CreateAnd(NaturalContinue, AnyActive, "loop.continue"),
        {NaturalContinue, AnyActive});
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
  ArrayRef<BasicBlock *> ExitBlocks = getExitBlocks(C);
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
  // Debug aid: with `FEME_DEBUG_LINEARIZE_TRACE` set in the environment,
  // print this cycle's own Header/Latch/ExitBlock names, and (below,
  // right after `OtherCondBrBlocks` is computed) whether the header/latch
  // exit checks and every other in-cycle conditional branch are
  // classified divergent. Added while root-causing roadmap `L277` (all 30
  // `dEQP-VK.glsl.loops.special.*_dynamic_iterations.{nested*,
  // dowhile_trap}` residual failures): confirms that for a *nested*
  // (non-leaf) divergent cycle, `linearizeCyclePostOrder`'s own
  // `CI.children(C).empty()` guard (see that function's own long comment
  // on why non-leaf traversal stays disabled -- "bug 5") means this trace
  // never even prints for the outer cycle at all, only its inner child --
  // the outer cycle is left completely untouched, exactly the
  // `feme-cpu-simdize: ... has a divergent branch` error every one of
  // these cases hits at pipeline-creation time.
  if (::getenv("FEME_DEBUG_LINEARIZE_TRACE"))
    errs() << "L277TRACE: cycle Header=" << Header->getName()
           << " Latch=" << Latch->getName()
           << " ExitBlock=" << ExitBlock->getName() << "\n";


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

  // Roadmap K6v: `StructurizeCFG` can leave a loop's own *real*, uniform
  // trip-count check sitting one hop upstream of `Latch` -- in a genuine
  // `CondBrInst` block whose only "stay in the loop" edge targets a
  // trivial, phi-less relay stub (`Latch` itself, doing nothing but
  // `br label %Header`) -- rather than making that check `Latch`'s own
  // terminator directly. Neither `matchExitCheck` (requires `Latch`'s own
  // terminator to be the check) nor `OtherCondBrBlocks`'s existing
  // `matchExitCheckWithRelay`-based recovery below (reserved for a
  // genuinely *divergent* relocated check -- see `DivergentCandidates`'s
  // own comment) recognizes this uniform shape, so without this fold the
  // cycle falls all the way through to the final "no recognized uniform
  // exit check" diagnostic. Confirmed via a real, reproduced `dEQP-VK.
  // spirv_assembly.instruction.compute.variable_pointers.
  // vars_ssbo_2_diff` IR dump: its own real check, `icmp sge i32 %11,
  // %1`, lives in exactly this shape, one block upstream of the trivial
  // `._crit_edge2` relay `Latch` resolves to. Eliminate the trivial relay
  // and let its own unique predecessor take over the `Latch` role
  // directly -- afterwards, `matchExitCheck(*Latch, ExitBlock)` finds the
  // check exactly where it now looks for it, with no further special
  // casing required anywhere else in this function.
  //
  // Deliberately placed *after* the fixed-point
  // `foldRedundantFlowBlocksInCycle`/`peelConstantFlowPredecessorsInCycle`/
  // `collapseTriviallyRedundantPhisInCycle`/`mergeTrivialRelayBlocksInCycle`
  // loop above, not before it: a `StructurizeCFG`-rotated "guard loop"
  // shape (`CompiledStageTest.LoopWithBarrierSurvivesCompiledStageCreation`'s
  // own shape) can have a shared `Flow`-style dispatch block -- a genuine
  // `CondBr` on a fully compile-time-constant-per-edge `phi` -- sitting
  // exactly where this fold looks for a relocated check, until
  // `foldRedundantFlowBlocksInCycle` constant-folds it away into the
  // plain pass-through it really is. Running this fold first would
  // promote that spurious dispatch block instead, double-handling
  // `Header`'s own, separately-and-already-correctly-recovered check (a
  // real regression this ordering fixes).
  //
  // Restricted to:
  //   * a *uniform* `Pred` branch (`!isDivergentBranch`) -- a genuinely
  //     divergent one relocated the same way is already correctly
  //     recognized and handled by the `OtherCondBrBlocks`/
  //     `DivergentCandidates`/`RealExitCandidates` machinery below, with
  //     its own, more involved "never really exit, defer to Latch"
  //     narrowing semantics that this plain structural promotion must not
  //     bypass (confirmed via a real regression against
  //     `LinearizesCheckBlockChainedDivergentCheckBypassToStayInLoop`'s
  //     own captured shape, whose own `Flow71`/`Flow71._crit_edge` pair
  //     matches this fold's structural shape exactly but is genuinely
  //     divergent and must keep taking that other path instead);
  //   * a *direct* match (`Promoted->RelayBlock == Pred`, i.e. `Pred`'s
  //     own exit arm reaches `ExitBlock` with no further relay of its
  //     own) specifically so that guarantee continues to hold once
  //     `Latch` is reassigned below; a still-relayed match is left for a
  //     future milestone rather than risked here;
  //   * `Header` itself having *no* recoverable check of its own (neither
  //     a direct `matchExitCheck` nor a relayed `recoverRelayExitCheck`
  //     match) -- belt-and-braces alongside the reordering above, so a
  //     future fold inserted between the two continues to get the same
  //     protection without relying solely on ordering.
  //
  // `LatchWasPromoted` (recorded below) distinguishes this promoted shape
  // from the ordinary "`Latch` already naturally owns its own real check"
  // shape (e.g. `loop-body-diamond.ll`'s `merge` block, needing no
  // promotion at all) for the "uniform loop with mask ops" branch further
  // down, which must keep treating that ordinary shape exactly as before
  // (a real regression found against `loop-body-diamond.ll` when that
  // branch's own guard did not yet distinguish the two).
  bool LatchWasPromoted = false;
  if (Latch->phis().empty() && isPureRelayBlock(Latch) &&
      !matchExitCheck(*Header, ExitBlock) &&
      !recoverRelayExitCheck(Header, C, ExitBlock)) {
    if (BasicBlock *Pred = Latch->getUniquePredecessor()) {
      auto *PredBr = dyn_cast<CondBrInst>(Pred->getTerminator());
      if (PredBr && CI.contains(C, Pred) && Pred != Header &&
          !isDivergentBranch(PredBr)) {
        if (std::optional<ExitCheck> Promoted =
                matchExitCheckWithRelay(*Pred, ExitBlock);
            Promoted && Promoted->StayInLoop == Latch &&
            Promoted->RelayBlock == Pred) {
          for (PHINode &PN : Header->phis())
            if (int Idx = PN.getBasicBlockIndex(Latch); Idx != -1)
              PN.setIncomingBlock(Idx, Pred);
          PredBr->setSuccessor(PredBr->getSuccessor(0) == Latch ? 0 : 1,
                               Header);
          Latch->eraseFromParent();
          Latch = Pred;
          LatchWasPromoted = true;
        }
      }
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
  auto makeActivePNPair = [&](bool SeedBackedgeWithPoison = true) {
    PHINode *LivePN =
        PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active.live");
    LivePN->insertBefore(Header->getFirstNonPHIIt());
    PHINode *SideEffectPN =
        PHINode::Create(I1Ty, /*NumReservedValues=*/2, "active.sideeffect");
    SideEffectPN->insertBefore(Header->getFirstNonPHIIt());
    // Roadmap L286: an enclosing divergent diamond `DiamondFlattener`
    // already flattened, before this pass ever ran, may have narrowed
    // which lanes should even be considered live entering this cycle at
    // all -- see `DiamondFlattenedEntryMasks`'s own comment. Prefer that
    // recorded mask over the bare, unconditional `true` this pass would
    // otherwise always seed a non-backedge predecessor with.
    //
    // Roadmap L440: a found-but-invalid entry means whatever this pass's
    // own processing of some other, earlier-linearized cycle did since
    // has erased the instruction(s) it referred to -- fall back to the
    // unconditional-`true` seeding exactly as if nothing had ever been
    // recorded, rather than threading a dangling `Value *` into a brand
    // new `PHINode`'s incoming-value list.
    auto It = DiamondFlattenedEntryMasks.find(Header);
    bool HasFlattenedMask =
        It != DiamondFlattenedEntryMasks.end() && It->second.isValid();
    MaskPair FlattenedMask =
        HasFlattenedMask ? It->second.get() : MaskPair{nullptr, nullptr};
    for (BasicBlock *Pred : predecessors(Header)) {
      if (CI.contains(C, Pred))
        continue;
      if (HasFlattenedMask) {
        LivePN->addIncoming(FlattenedMask.Live, Pred);
        SideEffectPN->addIncoming(FlattenedMask.SideEffect, Pred);
      } else {
        LivePN->addIncoming(ConstantInt::getTrue(Ctx), Pred);
        SideEffectPN->addIncoming(ConstantInt::getTrue(Ctx), Pred);
      }
    }
    // Roadmap L419(w): seed `Latch`'s own incoming slot with a `poison`
    // placeholder right away, rather than leaving it entirely absent
    // until `addLatchIncoming` fills it in far below (after the whole
    // cycle's exit check(s) are linearized) -- `isKnownConstantMask`'s
    // `lookThroughTrivialPhi` helper otherwise sees this phi as having
    // only a single, constant-`true` incoming value (every non-backedge
    // predecessor above) while it is in this still-incomplete state, and
    // folds it to that constant -- exactly as if every lane really were
    // unconditionally active -- for any `applyStageMasks`/
    // `collectUniformPassThroughRegion` use of it that runs before
    // `addLatchIncoming` ever does (every "`PreRegion`"/"`PostRegion`"
    // masking loop in `linearizeCycle` below). A `poison` placeholder
    // breaks that false "all-same" classification immediately: it is not
    // itself an incoming value `applyStageMasks`/`collectUniformPass
    // ThroughRegion` ever observes directly (every use of `Masks.Live`
    // they narrow a call's mask operand *to* reads the phi's own result,
    // never one specific incoming edge), and `addLatchIncoming` below
    // overwrites it with the real, computed per-iteration value before
    // this function returns, so no use ever actually sees the poison
    // itself. Roadmap `L419(u)`'s own prior attempt at this same seed
    // regressed `select_iteration_count_{fragment,vertex}` because
    // nothing yet re-examined a `PreRegion`/`PostRegion` block whose own
    // internal branch turned out to be mask-dependent once narrowed this
    // way -- `flattenTaintedBranchesInRegion` (see its own comment) is
    // the fix for that other half; this seed alone is unsound without it.
    //
    // Roadmap L419(w): \p SeedBackedgeWithPoison lets a caller that has
    // already independently established (via `CycleNeedsRealMasking`,
    // see its own comment) that *this* cycle has no genuinely divergent
    // exit check anywhere -- neither `Header`/`Latch` nor any
    // `OtherCondBrBlocks` member -- opt back out of the poison seed
    // entirely, in favor of the plain constant `true` this function
    // always used pre-`L419(w)`. Skipping the poison seed for exactly
    // such a cycle avoids tainting any resource/image load inside it
    // with a syntactically non-constant (even though semantically
    // always-true) mask, which otherwise makes an ordinary, always-
    // uniform internal branch elsewhere in the loop body (a
    // `conditional_break`/`dowhile_trap`/nested loop's own break check,
    // none of which ever needed masking in the first place) look like a
    // flatten candidate to `flattenTaintedBranchesInRegion` for no
    // actual reason.
    Value *BackedgeSeed = SeedBackedgeWithPoison
                              ? static_cast<Value *>(PoisonValue::get(I1Ty))
                              : static_cast<Value *>(ConstantInt::getTrue(Ctx));
    LivePN->addIncoming(BackedgeSeed, Latch);
    SideEffectPN->addIncoming(BackedgeSeed, Latch);
    return MaskPair{LivePN, SideEffectPN};
  };
  const DataLayout &DL = F.getDataLayout();
  auto addLatchIncoming = [&](MaskPair &Masks, const MaskPair &AtLatch) {
    cast<PHINode>(Masks.Live)->setIncomingValueForBlock(Latch, AtLatch.Live);
    cast<PHINode>(Masks.SideEffect)
        ->setIncomingValueForBlock(Latch, AtLatch.SideEffect);
    // Roadmap L419(w): now that both of this phi's incoming edges are
    // genuinely known (unlike `isKnownConstantMask`'s own early check,
    // which only ever saw the still-poison-seeded placeholder above), see
    // whether it turned out this cycle's masking never actually needed to
    // become non-constant at all (e.g. a real exit check that itself
    // never narrows `active.live`/`active.sideeffect`, as opposed to
    // `select_iteration_count`'s genuinely divergent one) -- and if so,
    // fold it (and anything that becomes foldable as a direct result)
    // back down, so the poison seed's broadened non-constant-ness never
    // leaks into IR that never actually needed it.
    PHINode *MaskPhis[] = {cast<PHINode>(Masks.Live),
                           cast<PHINode>(Masks.SideEffect)};
    {
      // Roadmap L419(w): `Masks` is held by reference specifically so
      // this fold's own replacement values (if any of `MaskPhis` itself
      // gets folded away -- see this function's own doc comment for the
      // dangling-pointer bug this fixes) propagate back to every one of
      // `linearizeCycle`'s own later uses of `Masks.Live`/
      // `Masks.SideEffect`, not just the two local copies above.
      SmallVector<Value *, 2> Folded =
          foldNowTriviallyConstantMaskPhis(CI, C, DL, MaskPhis);
      Masks.Live = Folded[0];
      Masks.SideEffect = Folded[1];
    }
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
    if (!HeaderExit || !isDivergentBranch(HeaderExit->Br))
      return false; // No divergence: leave this real uniform loop alone.

    MaskPair Masks = makeActivePNPair();
    applyStageMasks(*Header, Masks);
    IRBuilder<> B(HeaderExit->Br);
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(HeaderExit->Cond)
                                            : HeaderExit->Cond;
    MaskPair MasksNext = stayInLoop(B, Masks, Staying, "active.next");
    Value *Continue = createUniformMaskAny(B, MasksNext.Live, "loop.continue");
    CondBrInst::Create(Continue, HeaderExit->StayInLoop, ExitBlock,
                       HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
    freezeLoopCarriedValues(
        Header, Latch, MasksNext.Live,
        {cast<PHINode>(Masks.Live), cast<PHINode>(Masks.SideEffect)},
        [&](const BasicBlock *BB) { return CI.contains(C, BB); });
    addLatchIncoming(Masks, MasksNext);
    // Roadmap L282: see the identical comment at the other two success
    // paths' own `return true` -- a single-block loop can never itself be
    // an enclosing region for a nested child cycle (it has no other
    // blocks), but it can still *be* one, so its own entry masks are
    // recorded here too.
    HeaderActiveMasks[Header] = Masks;
    return true;
  }

  // Roadmap H165: a divergent internal branch that has no bearing on the
  // loop's own iteration decision at all -- i.e. it does not even match
  // `matchExitCheckWithRelay` -- is an ordinary diamond that happens to
  // reconverge before reaching the loop's header, latch, or exit block,
  // the same shape `feme::cpu::DiamondFlattener` already flattens outside
  // a loop. Flatten each one found up front (to a fixed point: flattening
  // one can turn what looked like a second `OtherCondBrBlocks` entry into
  // an ordinary uniform pass-through, though the common case is exactly
  // one), leaving only this cycle's own genuine divergent exit check (if
  // any) for the classification below -- this is what closes
  // `InterlockedExchange.resources.32.test`'s own two-diamond
  // monotonicity loop: one diamond, gated on an atomic's per-lane result,
  // is genuinely divergent and reconverges mid-body (flattened here); the
  // other, deeper in the same body, is a plain post-barrier uniform load
  // comparison already tolerated by the "leave alone" path below.
  {
    // Roadmap L198: `DT`/`PDT` were last recomputed once, at
    // `linearizeCyclePostOrder`'s own entry, *before* this same
    // `linearizeCycle` call ran any of the fold/peel/merge fixed-point
    // logic above (`foldRedundantFlowBlocksInCycle`/
    // `peelConstantFlowPredecessorsInCycle`/
    // `collapseTriviallyRedundantPhisInCycle`/
    // `mergeTrivialRelayBlocksInCycle`) -- and those genuinely
    // `eraseFromParent()` blocks (a redundant "Flow" re-derivation, or a
    // now-phi-less relay merged into its predecessor) whenever they find
    // one. Constructing `DiamondFlattener` with that same, now-stale
    // `PDT` here would let its own `immediatePostDom` calls (see
    // `flattenLoopBodyDiamond`) return a dangling `BasicBlock*` for any
    // reconvergence point whose post-dominator answer routed through one
    // of those just-erased blocks -- confirmed via a real, reproducible
    // (if heap-layout-dependent, hence flaky) crash: `validate`'s
    // `R->hasNPredecessors(2)` check hitting a `hasUseList()` assertion
    // failure on such a dangling `R`. Recomputing both here, immediately
    // before `DiamondFlattener` is built (and hence before any
    // `immediatePostDom` call it makes), closes that gap: everything
    // above that can delete a block has already run, and nothing below
    // this point deletes one before `DF` is done using `PDT` (`flatten`
    // itself only ever rewires edges and erases *instructions* --
    // branches, phis -- never a whole `BasicBlock`, see its own comment).
    // Same "cheap enough to always do, rather than track exactly what
    // changed" reasoning as the per-cycle recompute in
    // `linearizeCyclePostOrder` -- cycles are typically small, and this
    // fixes a genuine crash, not just a latent hazard.
    DT.recalculate(F);
    PDT.recalculate(F);
    // Roadmap L197: `DiamondFlattener::isLoopControlEdge` classifies loop-
    // control edges via a live `CI.contains` membership check (O(1), no
    // block-list walk) rather than any `CI.getExitBlocks`-derived list --
    // see its own comment for why both a live call and a precomputed
    // cache of that method were tried and rejected here (a genuine
    // use-after-free for the former, and staleness against blocks this
    // same fixed-point loop itself inserts for the latter). No extra
    // plumbing is needed at this call site as a result.
    DiamondFlattener DF(F, DT, PDT, CI, UI, MergeVisited, ResolvedBlocks);
    bool FlattenedAny = true;
    while (FlattenedAny) {
      FlattenedAny = false;
      for (BasicBlock &BB : F) {
        if (!CI.contains(C, &BB) || &BB == Header || &BB == Latch)
          continue;
        auto *CondBr = dyn_cast<CondBrInst>(BB.getTerminator());
        if (!CondBr || PeeledFrom.contains(&BB) ||
            !isDivergentBranch(CondBr))
          continue;
        // Whether `BB` is a genuine exit check (one arm reaches
        // `ExitBlock`, possibly via a relay chain) is `flattenLoopBodyDiamond`'s
        // own concern to rule out already -- it requires *both* arms to
        // reconverge without crossing a loop control edge at all (see its
        // own comment), which a real exit check's own arm toward
        // `ExitBlock` always violates. No need to duplicate that check
        // with `matchExitCheckWithRelay` here first.
        if (DF.flattenLoopBodyDiamond(&BB)) {
          FlattenedAny = true;
          break; // The scan below is stale the moment any block's
                 // terminator changes; restart it.
        }
      }
    }
  }

  // Roadmap L292: set below (inside the `OtherCondBrBlocks`-non-empty
  // handling) whenever this cycle has a genuinely divergent internal
  // branch that is *not* a second exit check (see that block's own
  // comment) -- such a branch leaves a real `CondBr` with a
  // `UniformityInfo`-divergent condition sitting in the final IR unless
  // the uniform-loop-with-body-mask-ops handling below (which, despite
  // its name, lowers *any* leftover divergent internal diamond via
  // `DiamondFlattener::flattenLoopBodyRegion`, not just ones containing a
  // `feme.stage.*` op) also runs for it, which that handling's own
  // `CycleHasMaskOps`-only gate would otherwise incorrectly skip when the
  // loop has no mask op at all (confirmed via
  // `unsupported-loop-internal-branch.ll`'s own divergent `continue`
  // check, which has none).
  bool HasDeferredDivergentCandidates = false;

  // Every other cycle block, if any, must instead be the single "Flow
  // merge" exit-check block described above (see the file comment).
  //
  // Computed *before* `HeaderExit`/`LatchExit` below (rather than at its
  // original point of use, inside the `OtherCondBrBlocks`-non-empty
  // handling) specifically so `DivergentCandidates` (see just below) is
  // available to gate whether `recoverRelayExitCheck`'s own structural
  // fallback is safe to try on `Header`/`Latch` at all -- see that
  // decision's own comment.
  SmallVector<BasicBlock *, 2> OtherCondBrBlocks;
  for (BasicBlock &BB : F)
    if (CI.contains(C, &BB) && &BB != Header && &BB != Latch &&
        isa<CondBrInst>(BB.getTerminator()))
      OtherCondBrBlocks.push_back(&BB);

  // Roadmap L292: a genuinely divergent branch elsewhere in the cycle,
  // unrelated to `Header`/`Latch` (see the comment on `OtherCondBrBlocks`
  // above for why such a block is tolerated here at all). Computed ahead
  // of its original point of use for the same reason as
  // `OtherCondBrBlocks` itself -- see that comment.
  SmallVector<BasicBlock *, 2> DivergentCandidates;
  for (BasicBlock *BB : OtherCondBrBlocks)
    if (!PeeledFrom.contains(BB) &&
        isDivergentBranch(cast<CondBrInst>(BB->getTerminator())))
      DivergentCandidates.push_back(BB);

  // Roadmap L292: a plain, direct-successor `matchExitCheck` can miss
  // `Header`'s (or `Latch`'s) own real exit check entirely when
  // `StructurizeCFG` has routed both the loop's real "continue" edge and
  // its real "exit" edge through a single shared `Flow`-style dispatch
  // block -- see `recoverRelayExitCheck`'s own comment for why a
  // structural recovery, not `matchExitCheckWithRelay`, is what is safe to
  // try here. This recovery is shape-only: it says nothing about whether
  // the recovered check's own condition is actually divergent, so it
  // correctly leaves `HeaderDivergent`/`LatchDivergent` below to tell a
  // genuinely divergent header/latch exit (`loop-break-structurized.ll`'s
  // own shape) apart from a uniform one merely obscured by the same relay
  // (`dynamic_loop_always`'s shape, handled further below).
  //
  // Always attempted (not gated on whether a separate divergent candidate
  // also exists elsewhere in the cycle): `dEQP-VK.glsl.loops.special.
  // for_dynamic_iterations.conditional_break_fragment`'s own shape -- a
  // genuinely divergent per-fragment trip count hidden behind exactly
  // this kind of relay *and* a separate, genuinely divergent `break`
  // check elsewhere, each relayed to `ExitBlock` independently -- is a
  // real, supported combination (see the `DivergentCandidates`-non-empty
  // handling below, which now recognizes and masks `HeaderExit`/
  // `LatchExit`'s own recovered check as a first-stage, sequential "never
  // really exit, just narrow the mask" filter ahead of the loop's single
  // other genuine exit check, rather than bailing out on it).
  std::optional<ExitCheck> HeaderExit = matchExitCheck(*Header, ExitBlock);
  if (!HeaderExit)
    HeaderExit = recoverRelayExitCheck(Header, C, ExitBlock);
  std::optional<ExitCheck> LatchExit = matchExitCheck(*Latch, ExitBlock);
  if (!LatchExit)
    LatchExit = recoverRelayExitCheck(Latch, C, ExitBlock);
  // Roadmap L432(a): neither of the two attempts above fire at all unless
  // `Header`/`Latch` itself owns a `CondBr` terminator -- but `Header` can
  // be a genuinely trivial, single-successor pass-through block owning
  // only the loop's induction `phi`, with its real, uniform trip-count
  // check sitting one or more pure-relay hops further forward (see
  // `findForwardExitCheckBlock`'s own comment). Only tried once the two
  // direct attempts above have already failed, and only for `Header`:
  // `Latch`'s own terminator is already required elsewhere in this
  // function to be the cycle's real backedge-deciding block, so a
  // forward walk from it would only ever either immediately hit `Header`
  // again (an unconditional single-block loop body, already handled
  // directly above) or escape the cycle.
  //
  // Roadmap L432(c): restricted to a *non-divergent* match only --
  // `loop-check-separate-carried-value.ll`'s own "check separate from
  // header/latch" shape has a genuinely divergent check sitting forward
  // of `Header` exactly like this, but that shape is already correctly
  // (and more completely -- including its own mask-narrowing machinery)
  // handled below by `OtherCondBrBlocks`/`DivergentCandidates`/
  // `CheckBlock`. Accepting a divergent forward match here too let both
  // code paths claim the very same block as their own exit check,
  // crashing (a double mutation of the same, already-rewritten `CondBr`)
  // the moment `linearizeCycle`'s `CheckBlock` handling ran right after
  // this function's own `Header`-based handling had already rewritten
  // it. The uniform case this helper actually exists for (see its own
  // comment) never has a genuinely divergent forward match in the first
  // place, so this restriction costs it nothing.
  if (!HeaderExit) {
    if (BasicBlock *Forward =
            findForwardExitCheckBlock(Header, Latch, ExitBlock, C)) {
      std::optional<ExitCheck> ForwardExit = matchExitCheck(*Forward, ExitBlock);
      if (!ForwardExit)
        ForwardExit = recoverRelayExitCheck(Forward, C, ExitBlock);
      if (ForwardExit && !isDivergentBranch(ForwardExit->Br))
        HeaderExit = ForwardExit;
    }
  }
  bool HeaderDivergent = HeaderExit && isDivergentBranch(HeaderExit->Br);
  bool LatchDivergent = LatchExit && isDivergentBranch(LatchExit->Br);

  if (::getenv("FEME_DEBUG_LINEARIZE_TRACE")) {
    errs() << "L277TRACE: HeaderDivergent=" << HeaderDivergent
           << " LatchDivergent=" << LatchDivergent
           << " OtherCondBrBlocks=[";
    for (BasicBlock *BB : OtherCondBrBlocks) {
      errs() << BB->getName() << " (divergent="
             << isDivergentBranch(cast<CondBrInst>(BB->getTerminator()))
             << ") ";
    }
    errs() << "]\n";
  }

  // Roadmap L419(w): whether this cycle will need any genuinely
  // non-constant `active.live`/`active.sideeffect` masking at all --
  // true iff `Header`'s/`Latch`'s own exit check, or any
  // `OtherCondBrBlocks` member (`DivergentCandidates`'s own, already-
  // filtered subset), is itself divergent. Gating entry into the whole
  // `DivergentCandidates`-non-empty branch below on this (rather than
  // merely `!OtherCondBrBlocks.empty()`) matters now that `Masks` is
  // seeded with a `poison` placeholder (see `makeActivePNPair`'s own
  // comment) rather than a plain constant `true`: that placeholder makes
  // `isKnownConstantMask` treat `Masks.Live`/`.SideEffect` as non-constant
  // from the moment they are created, which `applyStageMasks`'s
  // `narrowCallMask` then threads into *any* resource/image load inside
  // this cycle regardless of whether the cycle turns out divergent at
  // all -- tainting a load's mask operand even for a cycle every member
  // of which is already known, right here, to be genuinely uniform. That
  // taint then makes `flattenTaintedBranchesInRegion` (see its own
  // comment) treat an ordinary, already-correctly-left-alone uniform
  // internal branch (e.g. a `conditional_break`/`dowhile_trap`/nested
  // loop's own break check, none of which ever needed any masking in the
  // first place) as a flatten candidate, which --- once
  // `flattenKnownDivergentBranch`'s own cycle-containment check (see its
  // comment) correctly refuses to unsoundly flatten a branch whose taken
  // arm escapes the cycle --- surfaces as a brand new "does not have the
  // simple shape this pass can flatten" diagnostic for a shader that
  // always compiled and rendered correctly before any of this masking
  // infrastructure existed. Skipping the whole branch below for a
  // cycle with no genuine divergence anywhere routes it to the same
  // plain, poison-free `DivergentCandidates`-empty handling at the
  // bottom of this function instead -- exactly the code path it always
  // took pre-`L419(w)` and should keep taking. Found reducing the net
  // regression `dEQP-VK.glsl.loops.special.*`'s own
  // `{do_while,for,while}_uniform_iterations.conditional_break_
  // {fragment,vertex}`, `do_while_dynamic_iterations.{dowhile_trap,
  // nested,nested_sequence,nested_tricky_dataflow_{1,2}}_{fragment,
  // vertex}` cases newly introduced by this session's own poison-seed
  // widening.
  //
  // Also true whenever `cycleHasMaskOps(C)` -- a resource/image/
  // `StageOpKind` call that itself needs masking sits somewhere in this
  // cycle's body -- even if every branch above reports non-divergent:
  // `loop-internal-break-with-mask-op.ll`'s own shape is a body-level
  // break keyed off an ordinary (not cross-lane-divergent) value, yet
  // still requires the break to narrow `active.sideeffect` so a lane
  // that already broke out on an earlier iteration does not keep
  // re-executing this call's side effect on every later iteration --
  // masking correctness here does not depend on the break's condition
  // being *provably* divergent, only on there being a side effect to
  // mask at all.
  bool CycleNeedsRealMasking = HeaderDivergent || LatchDivergent ||
                               !DivergentCandidates.empty() ||
                               cycleHasMaskOps(C);

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
    //
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
    // Roadmap L197: an already-linearized nested child cycle's own
    // `Header`/`Latch` (see `linearizeCyclePostOrder`'s own comment) is
    // exactly one more reason a block here can be non-divergent despite
    // still ending in a real `CondBrInst`: `closeLatch` always finalizes
    // a fully-linearized cycle's own continuation check on a value this
    // pass itself already recorded in `KnownUniformValues`, so
    // `isDivergentBranch` correctly reports it as *not* divergent here,
    // the same as any other genuine compile-time-uniform pass-through
    // block -- computed by the exact same `DivergentCandidates` filter
    // below, with no separate case needed.
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
    //
    // (`DivergentCandidates` itself is computed earlier now, alongside
    // `OtherCondBrBlocks`, so it can also gate whether
    // `recoverRelayExitCheck` is safe to try on `Header`/`Latch` -- see
    // that decision's own comment.)

    // Roadmap L292: only bail out here if `Latch`'s own exit check is
    // *itself* genuinely divergent -- `HeaderDivergent` coexisting with a
    // separate genuine exit check elsewhere is instead handled below (see
    // the "Roadmap L292: `Header`'s own exit check" comment ahead of
    // `CheckBlock`'s own handling) as long as `Latch` does not *also* have
    // its own divergent check: two separate divergent checks, each
    // independently requiring this "never really exit, narrow the mask
    // instead" treatment, both coexisting with yet a *third* in
    // `OtherCondBrBlocks`, is the genuinely harder shape this milestone
    // does not yet support.
    if (!DivergentCandidates.empty() && LatchDivergent) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" +
                      DivergentCandidates.front()->getName() +
                      "'; unsupported (roadmap milestone 6 deviation)");
      return false;
    }

    // Roadmap L292: a `DivergentCandidates` entry that does not itself
    // reach `ExitBlock` (via a relay chain or otherwise) is not a second
    // exit check at all -- it is an ordinary divergent diamond entirely
    // inside an otherwise-uniform loop body (see `dEQP-VK.glsl.demote.
    // dynamic_loop_{dynamic,deriv,texture}`'s own `if (divergent
    // condition) demote` nested inside a uniform `if (i > 0)`), one more
    // of which can coexist with another in a shader using more than one
    // such guarded op (confirmed by `dynamic_loop_deriv`'s own two
    // `discard`-affecting branches). Partition out any genuine exit
    // candidate(s) from these first, so the "more than one divergent exit
    // check" diagnostic below only ever fires for genuinely ambiguous
    // *exit* shapes, not for however many plain internal diamonds a loop
    // body happens to have -- those are instead deferred, as a group, to
    // the uniform-loop-with-body-mask-ops handling below, which threads
    // this cycle's real, loop-carried mask through the *entire*
    // `HeaderStayInLoop`-to-`Latch` region (including however many such
    // diamonds it contains) in one `DiamondFlattener::
    // flattenLoopBodyRegion` walk, rather than this block's own
    // exit-check-specific machinery (which cannot handle an arm that
    // never reaches `ExitBlock` at all). (`flattenLoopBodyDiamond`'s own
    // fixed-point pre-pass above already skips flattening any of these
    // prematurely whenever it contains a mask-affecting op -- see its own
    // comment -- which is exactly why a real `CondBr` survives here for
    // this scan to find in the first place.)
    SmallVector<BasicBlock *, 2> RealExitCandidates;
    for (BasicBlock *BB : DivergentCandidates)
      if (matchExitCheckWithRelay(*BB, ExitBlock))
        RealExitCandidates.push_back(BB);

    if (RealExitCandidates.size() > 1) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has more than one divergent exit check ('" +
                      RealExitCandidates[0]->getName() + "' and '" +
                      RealExitCandidates[1]->getName() +
                      "'); unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    if (RealExitCandidates.empty() && cycleHasMaskOps(C) &&
        OtherCondBrBlocks.size() == 1) {
      // Roadmap L419(h): a body-level break check whose own condition is
      // *uniform* (not divergent) is invisible to `DivergentCandidates`
      // above (filtered by `isDivergentBranch`), so it never reaches
      // `RealExitCandidates`'s own `matchExitCheckWithRelay` scan at all
      // -- yet the cycle still has a mask-affecting op in its body (a
      // resource/image/`StageOpKind` call `cycleHasMaskOps` recognizes),
      // meaning the loop-carried mask narrowing below (reached whenever
      // `RealExitCandidates` is non-empty) is exactly the machinery this
      // shape needs too, not the separate "fully uniform loop with body
      // mask ops" `DiamondFlattener`-based path further down.
      //
      // Roadmap L419(h): an earlier version of this widening (gated only
      // on `cycleHasMaskOps`, scanning every `OtherCondBrBlocks` member)
      // regressed `dEQP-VK.glsl.loops.special.*.mixed_break_continue_*`
      // -- a loop body lowering a `switch` into a `StructurizeCFG`-built
      // `NodeBlock`/`LeafBlock`/`Flow`/`Flow3`/`Flow4` diamond chain, all
      // uniform, whose `break`-leaf edge genuinely *does* bypass the
      // loop's own uniform trip-count re-check and branch straight to
      // `ExitBlock` (confirmed empirically via `FEME_DEBUG_L419H_TRACE=1`
      // below, which dumps the function and every `OtherCondBrBlocks`
      // candidate right before this scan runs: `Flow3.Flow4_crit_edge`'s
      // own false arm reaches `ExitBlock` through a direct edge that
      // bypasses `Latch` (`Flow5`) entirely, not by crossing it the way
      // this milestone first suspected -- so passing `Latch` as
      // `uniformRelayChain`'s `StopBlock` (still done below, as a
      // genuine second line of defense against a *different* shape that
      // really does loop back through `Latch`) does not reject it, and
      // the match is not a false positive at the `matchExitCheckWithRelay`
      // level at all). The actual problem is downstream: that shape's
      // existing "fully uniform loop with body mask ops"
      // `DiamondFlattener`-based fallback (further below) already
      // handles it correctly today, and diverting it into this block's
      // own `CheckBlock`-based handling instead (by giving
      // `RealExitCandidates` a non-empty entry) hits a *different*,
      // legitimate "peeled predecessor" diagnostic bailout this fix does
      // not support -- a clean diagnosed failure, not a crash or silent
      // miscompile, but still a regression from the previously-passing
      // fallback path. `conditional_break_fragment` (this widening's
      // actual target) has exactly *one* `OtherCondBrBlocks` member in
      // its entire cycle (`Flow` itself, with no further internal
      // diamonds at all); `mixed_break_continue_fragment`'s `switch`-
      // lowered body always has more (three here: `NodeBlock`, `Flow3`,
      // `Flow3.Flow4_crit_edge`), even though only one of them ends up
      // matching `matchExitCheckWithRelay`. Restricting this widening to
      // cycles whose *entire* `OtherCondBrBlocks` list has exactly one
      // member (not just one *match*) is therefore the precise
      // discriminator: it accepts the single-check shape this widening
      // was built for, while leaving any cycle with further internal
      // branching -- `switch`-lowered or otherwise -- to the fallback
      // path that already handles it.
      SmallVector<BasicBlock *, 2> WidenedCandidates;
      if (::getenv("FEME_DEBUG_L419H_TRACE")) {
        errs() << "L419HTRACE: scanning cycle Header='" << Header->getName()
               << "' Latch='" << Latch->getName() << "' ExitBlock='"
               << ExitBlock->getName() << "' in function '" << F.getName()
               << "'\n";
        for (BasicBlock *BB : OtherCondBrBlocks)
          errs() << "L419HTRACE: candidate block '" << BB->getName()
                 << "' terminator: " << *BB->getTerminator() << "\n";
      }
      for (BasicBlock *BB : OtherCondBrBlocks) {
        if (llvm::is_contained(DivergentCandidates, BB))
          continue;
        if (matchExitCheckWithRelay(*BB, ExitBlock, Latch)) {
          WidenedCandidates.push_back(BB);
          if (::getenv("FEME_DEBUG_L419H_TRACE"))
            errs() << "L419HTRACE: widened candidate match '"
                   << BB->getName() << "' in function '" << F.getName()
                   << "'\n";
        }
      }

      if (WidenedCandidates.size() == 1)
        RealExitCandidates.push_back(WidenedCandidates.front());
    }

    if (RealExitCandidates.empty()) {
      // Roadmap L197/L292: nothing here is a genuine divergent *exit*
      // check -- either this cycle has no divergent exit check at all
      // (the pre-existing "leave alone" case), `Header`/`Latch` still has
      // its own divergent check while every `OtherCondBrBlocks` entry is
      // already uniform (e.g. an already-linearized nested child cycle's
      // own continuation check -- see the comment above), or every
      // `DivergentCandidates` entry is instead an ordinary internal
      // diamond that never reaches `ExitBlock` (see the comment above).
      // Either way, nothing here needs this block's own special
      // handling: fall through to the ordinary `Header`/`Latch`-only
      // linearization below, which itself returns `false` (leave alone)
      // in the genuinely-nothing-divergent-or-mask-affecting case via its
      // own `!HeaderDivergent && !LatchDivergent`/`CycleHasMaskOps` check.
      HasDeferredDivergentCandidates = !DivergentCandidates.empty();
    } else {
    BasicBlock *CheckBlock = RealExitCandidates.front();
    // Roadmap L419(h): pass `Latch` as `StopBlock` here too, exactly as
    // the widening scan above already does when it is the one that
    // populated `RealExitCandidates` -- without this, a candidate whose
    // only route to `ExitBlock` requires `StopBlock`'s pruning to stay
    // unambiguous (see `uniformRelayChain`'s own comment) can re-resolve
    // as *ambiguous* here (both arms "reach" `ExitBlock` once the
    // backedge-through-`Latch` path is allowed again), turning this
    // `CheckExit` into `std::nullopt` even though the exact same
    // `CheckBlock` just matched uniquely above -- confirmed via a real
    // assertion failure on `CheckExit->StayInLoop`
    // (`loop-body-resolved-blocks-after-mask-op.ll`, a `DivergentCandidates`-
    // sourced `CheckBlock` this time, not a widened one, showing the
    // inconsistency is inherent to calling this same matcher two
    // different ways on one candidate, not specific to the widening's
    // own callers). Harmless for every pre-existing, non-widened
    // `CheckBlock` too: `StopBlock` only ever makes a match *more*
    // restrictive, and a genuine single real exit check never needs to
    // relay through its own cycle's `Latch` to reach `ExitBlock` in the
    // first place.
    std::optional<ExitCheck> CheckExit =
        matchExitCheckWithRelay(*CheckBlock, ExitBlock, Latch);

    // Roadmap L377: `CheckBlock` must be the sole gateway into its own
    // "stay in loop" continuation (`CheckExit->StayInLoop`) for the mask
    // narrowed just below (`MasksAfterCheck`, built from values computed
    // *inside* `CheckBlock`) to be valid at every point that
    // continuation reaches. An earlier fold in this same fixed-point
    // pipeline (`peelConstantFlowPredecessorsInCycle`/
    // `collapseTriviallyRedundantPhisInCycle`/
    // `mergeTrivialRelayBlocksInCycle`) can, for a provably-constant
    // edge, redirect some *other* block directly into
    // `CheckExit->StayInLoop` (or anything it reaches before `Latch`),
    // bypassing `CheckBlock`'s own check entirely -- confirmed via a
    // real, reproduced `verifyModule` dominance failure on `dEQP-VK.
    // pipeline.monolithic.image...127x1_compute`'s own doubly-nested
    // barycentric/degenerate-triangle check: a lane that takes that
    // bypass edge never computes `active.check.live`/
    // `exited.via.check.next` at all, so neither can possibly dominate
    // the phi/select later built from it. Recompute `DT` immediately
    // before checking (the `flatten()` fixed-point loop above can have
    // changed dominance since the last recompute); roadmap `L377(a)`
    // below recognizes and physically repairs the one shape this is
    // confirmed to actually arise from (a second, genuinely divergent
    // branch immediately upstream of `CheckBlock`, see its own comment),
    // still bailing out, diagnosed, for anything else rather than
    // silently producing invalid IR for a not-yet-understood shape.
    DT.recalculate(F);
    if (!DT.dominates(CheckBlock, CheckExit->StayInLoop)) {
      // Roadmap L377(a): the common, safe shape this is (confirmed via a
      // real IR dump of `dEQP-VK.pipeline.monolithic.image...
      // 127x1_compute`'s own doubly-nested barycentric/degenerate-
      // triangle check, post-`DiamondFlattener`): a second, genuinely
      // divergent branch (`ChainGate`) sits immediately upstream of
      // `CheckBlock` -- `CheckBlock`'s own *sole* predecessor -- with one
      // successor going straight to `CheckBlock` and the other (via a
      // uniform pass-through chain, `ChainBypassRegion`) bypassing it
      // entirely, straight to `CheckExit->StayInLoop`. This is not a real
      // second exit check (`ChainGate` itself never reaches `ExitBlock`
      // on either arm -- see `RealExitCandidates`'s own computation
      // above, which already ruled that out) -- it is an ordinary
      // sequential "if (c1) continue; else if (c2) continue; else exit;"
      // shape that `DiamondFlattener` could not flatten as an ordinary
      // body diamond (`flattenLoopBodyDiamond` correctly refuses: one of
      // `ChainGate`'s own arms, via `CheckBlock`'s *own* further branch,
      // can still reach `ExitBlock`, i.e. crosses a loop control edge --
      // see that function's own comment), yet is not itself the loop's
      // real exit decision either -- `CheckBlock` is. Recognize this
      // specific shape and physically eliminate `ChainGate`'s own
      // divergent branch below (rather than just diagnosing it), folding
      // its condition into `CheckBlock`'s own check as a plain boolean
      // value instead -- `feme-cpu-simdize` requires every divergent
      // branch gone by the time this pass finishes, not merely routed
      // around, so this cannot stop at making `CheckBlock` dominate its
      // continuation again; `ChainGate`'s own `CondBr` must disappear
      // too. The rest of this function's existing single-source
      // machinery keeps working entirely unmodified once that's done.
      BasicBlock *ChainGate = CheckBlock->getUniquePredecessor();
      CondBrInst *ChainGateBr =
          ChainGate ? dyn_cast<CondBrInst>(ChainGate->getTerminator())
                    : nullptr;
      BasicBlock *ChainBypassEntry = nullptr;
      std::optional<SmallPtrSet<BasicBlock *, 8>> ChainBypassRegion;
      BasicBlock *ChainBypassTail = nullptr;
      if (ChainGateBr && CI.contains(C, ChainGate) && ChainGate != Header &&
          ChainGate != Latch && isDivergentBranch(ChainGateBr) &&
          !matchExitCheckWithRelay(*ChainGate, ExitBlock)) {
        BasicBlock *S0 = ChainGateBr->getSuccessor(0);
        BasicBlock *S1 = ChainGateBr->getSuccessor(1);
        ChainBypassEntry = S0 == CheckBlock ? S1
                           : S1 == CheckBlock ? S0
                                              : nullptr;
      }
      if (ChainBypassEntry)
        ChainBypassRegion = collectUniformPassThroughRegion(
            ChainBypassEntry, CheckExit->StayInLoop, ExitBlock, C,
            PeeledFrom);
      // Roadmap L377(a): `collectUniformPassThroughRegion` only validates
      // the bypass's *control-flow* shape (a plain unconditional chain) --
      // it says nothing about what the blocks along it actually compute.
      // Physically deleting that chain below (required to make
      // `ChainGate`'s own branch disappear entirely, see the comment on
      // that below) is only safe if every block in it is pure glue: no
      // real instructions of its own (just its terminator) and no
      // predecessor besides the previous link in the chain -- otherwise a
      // lane that used to take this path could be computing (or someone
      // else could be relying on) a real value this milestone has no way
      // to preserve once the blocks producing it are gone. Both real,
      // captured shapes this fix was built from (the lit test's `76` and
      // the unit test's `86`) are exactly this: a bare `br` and nothing
      // else. Anything richer falls back to the original diagnostic
      // rather than risk silently dropping a live computation.
      if (ChainBypassRegion &&
          !llvm::all_of(*ChainBypassRegion, [](BasicBlock *BB) {
            return BB->size() == 1 && llvm::hasSingleElement(predecessors(BB));
          }))
        ChainBypassRegion = std::nullopt;
      if (ChainBypassRegion) {
        for (BasicBlock *P : predecessors(CheckExit->StayInLoop))
          if (P == ChainBypassEntry || ChainBypassRegion->contains(P)) {
            ChainBypassTail = P;
            break;
          }
        if (ChainBypassTail &&
            !llvm::all_of(predecessors(CheckExit->StayInLoop),
                          [&](BasicBlock *P) {
                            return P == CheckBlock || P == ChainBypassTail;
                          }))
          ChainBypassTail = nullptr; // Some third, unrecognized source too.
      }
      if (!ChainBypassTail) {
        diagnose(F, "loop at '" + Header->getName() +
                        "' has an internal branch in '" +
                        CheckBlock->getName() +
                        "' whose own check is not the sole entry into its "
                        "'stay in loop' continuation (e.g. a peeled "
                        "predecessor bypassing the check directly); "
                        "unsupported (roadmap milestone 6 deviation)");
        return false;
      }
      // Fold `ChainGate`'s own decision into a plain boolean *value*
      // (`MergedCond`, computed from `ChainGateBr`'s own condition while
      // it still dominates `CheckBlock`) rather than leaving it as real
      // control flow: `feme-cpu-simdize` requires every divergent branch
      // to have been removed by this point (see its own "has a divergent
      // branch" diagnostic), and `ChainGate`'s branch is exactly that --
      // a second, genuinely divergent `CondBr` -- so merely routing both
      // of its arms to the same block (this fix's first attempt) is not
      // enough; the branch itself must go away. Since the bypass chain is
      // now confirmed to be pure glue with no live effects of its own
      // (see above), nothing is lost by never actually taking it: build
      // the merged condition from `ChainGateBr`'s own condition value
      // directly, then replace `ChainGate`'s terminator with a plain,
      // uniform, unconditional branch straight into `CheckBlock`, and
      // delete the now wholly unreachable, side-effect-free bypass chain
      // outright.
      bool BypassOnTrue = ChainGateBr->getSuccessor(0) == ChainBypassEntry;
      Value *StayConst = CheckExit->ExitOnTrue ? ConstantInt::getFalse(Ctx)
                                               : ConstantInt::getTrue(Ctx);
      IRBuilder<> ChainB(ChainGateBr);
      Value *MergedCond =
          BypassOnTrue
              ? ChainB.CreateSelect(ChainGateBr->getCondition(), StayConst,
                                    CheckExit->Cond, "chain.merge")
              : ChainB.CreateSelect(ChainGateBr->getCondition(),
                                    CheckExit->Cond, StayConst, "chain.merge");
      CheckExit->StayInLoop->removePredecessor(ChainBypassTail,
                                               /*KeepOneInputPHIs=*/true);
      UncondBrInst::Create(CheckBlock, ChainGateBr->getIterator());
      ChainGateBr->eraseFromParent();
      for (BasicBlock *BB : *ChainBypassRegion)
        BB->eraseFromParent();
      CheckExit->Br->setCondition(MergedCond);
      CheckExit->Cond = MergedCond;
      DT.recalculate(F);
      assert(DT.dominates(CheckBlock, CheckExit->StayInLoop) &&
             "ChainGate should now be CheckBlock's sole, unconditional "
             "predecessor");
    }

    // Roadmap L292: `Header`'s own exit check (recovered via
    // `recoverRelayExitCheck`, since a plain `matchExitCheck` already
    // would have made this cycle take the ordinary `HeaderDivergent`
    // path further below instead of ever reaching here) can be
    // genuinely divergent *and* coexist with `CheckBlock`'s own,
    // separate divergent exit check -- confirmed on `dEQP-VK.glsl.loops.
    // special.for_dynamic_iterations.conditional_break_fragment`'s own
    // shape: a divergent per-fragment trip count hidden behind `Header`'s
    // own relay, together with a separate divergent `break` check
    // elsewhere in the body, each independently relayed to `ExitBlock`.
    // Apply the exact same "never really exit, just narrow the mask"
    // treatment to `Header`'s own check *first* -- before `CheckBlock`'s
    // own region even begins -- so `PreRegion` below starts its walk from
    // `HeaderExit->StayInLoop` (skipping `Header`'s own now-removed
    // `CondBr` entirely) rather than from `Header` itself, which
    // `collectUniformPassThroughRegion` would otherwise immediately
    // reject (a genuinely divergent `CondBr` strictly inside the region
    // it is asked to walk is exactly the shape that function refuses --
    // see its own comment).
    BasicBlock *PreRegionStart = Header;
    // Roadmap L297: `Header`'s own exit and `CheckBlock`'s own exit can
    // *both* capture a value for the *same* `ExitBlock` phi (e.g. a
    // per-lane flag distinguishing "exited via Header's uniform timeout"
    // from "exited via CheckBlock's own genuinely divergent condition" --
    // reduced from `Basic/Mandelbrot.test`'s own per-pixel escape-
    // iteration loop, whose post-loop `if (Diverged)` color selection
    // depends on exactly this). Tag each capture with its own origin so
    // the restore loop below can tell them apart: a flat "whichever
    // capture runs first wins" restore (this milestone's prior behavior)
    // silently drops the other value whenever the two genuinely differ,
    // which is wrong whenever that other value is runtime-varying (as
    // opposed to merely a second, redundant route to the same constant).
    enum class RelaySource { Header, Check };
    SmallVector<std::tuple<PHINode *, Value *, RelaySource>, 4>
        ExitBlockRelayValues;
    // Roadmap L419(ac): accumulates every `Header` phi
    // `freezeRelayChainExitValues` finds captured, raw, by a relay-chain
    // hop's own phi (see that function's own comment) -- forwarded to
    // this cycle's own `freezeLoopCarriedValues` call below so it also
    // applies its usual sticky, self-referential freeze to each one.
    SmallPtrSet<PHINode *, 4> RelayCapturedHeaderPhis;
    // Roadmap L419(w): see `CycleNeedsRealMasking`'s own comment -- a
    // cycle with a non-empty `OtherCondBrBlocks` but no genuine
    // divergence anywhere in it (this `CheckBlock` included, since it is
    // exactly `HeaderExit`/`LatchExit`'s own recovered relay check, not a
    // member of `OtherCondBrBlocks` itself) still reaches this handling
    // (it is gated on `OtherCondBrBlocks` being non-empty, not on
    // `CycleNeedsRealMasking`), so must still opt out of the poison seed
    // here explicitly.
    MaskPair EntryMasks = makeActivePNPair(/*SeedBackedgeWithPoison=*/
                                            CycleNeedsRealMasking);
    MaskPair Masks = EntryMasks;
    // Roadmap L297: a loop-carried flag, frozen like `EntryMasks` itself,
    // recording whether *this lane* has, by the current iteration,
    // exited via `CheckBlock`'s own check specifically (as opposed to
    // `Header`'s) -- seeded `false`, since no lane has exited via
    // `CheckBlock` before the loop even starts. Used below to correctly
    // merge, rather than arbitrarily pick between, `Header`'s and
    // `CheckBlock`'s own captured `ExitBlockRelayValues` whenever they
    // differ for the same phi.
    PHINode *ExitedViaCheck =
        PHINode::Create(I1Ty, /*NumReservedValues=*/2, "exited.via.check");
    ExitedViaCheck->insertBefore(Header->getFirstNonPHIIt());
    for (BasicBlock *Pred : predecessors(Header))
      if (!CI.contains(C, Pred))
        ExitedViaCheck->addIncoming(ConstantInt::getFalse(Ctx), Pred);
    // Roadmap L419(w): seed a placeholder for `Latch`'s own backedge
    // value right away, mirroring `makeActivePNPair`'s identical poison
    // seed just below (see its own comment for the full root-cause
    // writeup) -- `ExitedViaCheck`'s real backedge value is only known,
    // and only added, much later (see `setIncomingValueForBlock` below),
    // but this phi must already list every one of `Header`'s real
    // predecessors (`Latch` included) the moment it is created: several
    // `return false` paths between here and the real backedge update
    // (e.g. `flattenTaintedBranchesInRegion`'s own) leave whatever
    // mutations already happened permanently in place, and a `Header`
    // phi with a missing `Latch` entry at that point is malformed IR
    // forever after, not merely temporarily incomplete. Found reducing
    // `LinearizeTest.ExtendsExistingReconvergencePhiWhenASecondPeelAddsAThirdPredecessor`'s
    // own shape, whose `PreRegion` includes a branch this milestone
    // cannot yet flatten.
    ExitedViaCheck->addIncoming(PoisonValue::get(I1Ty), Latch);
    if (HeaderDivergent) {
      applyStageMasks(*Header, Masks);
      rethreadNestedEntryMasks(Header, Masks);
      for (PHINode &PN : ExitBlock->phis())
        if (int Idx = PN.getBasicBlockIndex(HeaderExit->RelayBlock);
            Idx != -1)
          ExitBlockRelayValues.emplace_back(&PN, PN.getIncomingValue(Idx),
                                            RelaySource::Header);
      IRBuilder<> HeaderCheckBuilder(HeaderExit->Br);
      Value *HeaderStaying = HeaderExit->ExitOnTrue
                                 ? HeaderCheckBuilder.CreateNot(HeaderExit->Cond)
                                 : HeaderExit->Cond;
      Masks = stayInLoop(HeaderCheckBuilder, Masks, HeaderStaying,
                         "active.header.check");
      // Roadmap L419(ac): must run before `HeaderExit->Br` below is
      // erased -- see `freezeRelayChainExitValues`'s own comment --
      // using `Masks.Live` exactly as just narrowed by `Header`'s own
      // check above (not any later, further-narrowed mask): it answers
      // precisely "is this a genuine, still-looping iteration for this
      // lane", which is exactly the question each relay-chain hop's own
      // now-orphaned phi needs answered.
      freezeRelayChainExitValues(HeaderExit->RelayChain, Header, Masks.Live,
                                 Header, RelayCapturedHeaderPhis);
      // See the identical, more thoroughly-commented pattern just below
      // for `CheckExit`'s own predecessor/phi bookkeeping -- this is a
      // no-op (as expected) whenever `Header` was only ever reaching
      // `ExitBlock` via `HeaderExit->RelayBlock`, never directly itself.
      if (llvm::is_contained(predecessors(ExitBlock), Header))
        ExitBlock->removePredecessor(Header, /*KeepOneInputPHIs=*/true);
      UncondBrInst::Create(HeaderExit->StayInLoop, HeaderExit->Br->getIterator());
      HeaderExit->Br->eraseFromParent();
      PreRegionStart = HeaderExit->StayInLoop;
    }

    std::optional<SmallPtrSet<BasicBlock *, 8>> PreRegion =
        collectUniformPassThroughRegion(PreRegionStart, CheckBlock, ExitBlock,
                                        C, PeeledFrom);
    std::optional<SmallPtrSet<BasicBlock *, 8>> PostRegion =
        collectUniformPassThroughRegion(CheckExit->StayInLoop, Latch,
                                        ExitBlock, C, PeeledFrom);
    if (!PreRegion || !PostRegion) {
      // Roadmap L419(h): `PreRegion`/`PostRegion` failing here (rather
      // than at `RealExitCandidates.size() > 1` above) almost always
      // means `Header`'s (or `Latch`'s) own terminator is itself a
      // second, genuinely divergent branch that `HeaderExit`/`LatchExit`
      // never recognized above -- not because its condition is uniform,
      // but because its own relay to `ExitBlock` is itself indirect
      // (e.g. funneled through `CheckBlock` rather than reaching
      // `ExitBlock` directly or via a *pure* relay chain, the only two
      // shapes `matchExitCheck`/`recoverRelayExitCheck` can recognize --
      // see their own comments). `while_loop_in_switch_dynamic_fragment`'s
      // own shape (`dEQP-VK.glsl.switch.loop_in_switch`) is exactly this:
      // `Header`'s own per-fragment-dynamic trip-count check is
      // genuinely divergent, but both of its successors merge into a
      // single shared `Flow`-style dispatch block before ever reaching
      // `ExitBlock`, so `HeaderExit` stays null and `HeaderDivergent`
      // below reads `false` purely because there is no recognized
      // `HeaderExit` to query -- not because `Header`'s real check is
      // uniform. Detect this directly (not relying on `HeaderExit`/
      // `LatchExit`) and emit the same "more than one divergent exit
      // check" diagnostic `RealExitCandidates.size() > 1` above already
      // uses, rather than this block's own generic "internal branch in
      // '<CheckBlock>'" message, which incorrectly blames `CheckBlock`
      // itself for a shape that is actually unsupported because of a
      // second divergent check elsewhere entirely.
      auto *HeaderBr = dyn_cast<CondBrInst>(Header->getTerminator());
      bool HeaderHasOwnDivergentCheck =
          Header != CheckBlock && HeaderBr && !HeaderExit &&
          !PeeledFrom.contains(Header) && isDivergentBranch(HeaderBr);
      auto *LatchBr = dyn_cast<CondBrInst>(Latch->getTerminator());
      bool LatchHasOwnDivergentCheck =
          Latch != CheckBlock && LatchBr && !LatchExit &&
          !PeeledFrom.contains(Latch) && isDivergentBranch(LatchBr);
      if (HeaderHasOwnDivergentCheck || LatchHasOwnDivergentCheck) {
        BasicBlock *Other =
            HeaderHasOwnDivergentCheck ? Header : Latch;
        diagnose(F, "loop at '" + Header->getName() +
                        "' has more than one divergent exit check ('" +
                        Other->getName() + "' and '" +
                        CheckBlock->getName() +
                        "'); unsupported (roadmap milestone 6 deviation)");
        return false;
      }
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

    for (BasicBlock *BB : *PreRegion) {
      applyStageMasks(*BB, Masks);
      rethreadNestedEntryMasks(BB, Masks);
    }
    // Roadmap L419(w): now that masking above may have narrowed a
    // resource/image load's mask inside `PreRegion`, re-examine every
    // block there for a `CondBr` that only *looked* uniform before that
    // narrowing happened (see `flattenTaintedBranchesInRegion`'s own
    // comment for the full root-cause writeup).
    if (!flattenTaintedBranchesInRegion(*PreRegion, Masks, C))
      return false;

    // Roadmap L442: `CheckBlock` is not purely "before" its own check --
    // any resource/image call or memory access physically sharing this
    // same block *at or after* `CheckExit->Cond`'s own instruction is
    // part of the iteration that check is deciding the validity of,
    // exactly like a bounded, uniform-looking trip-count check (`j < N`)
    // whose own `UniformityInfo` classification is still "divergent"
    // only because some other, genuinely divergent exit exists elsewhere
    // in this same cycle (see `isDivergentBranch`'s own roadmap L40
    // comment above). Compute `Staying`/`MasksAfterCheck` right after
    // `Cond`'s own instruction (not at `CheckExit->Br`, the block's
    // terminator, far below it) and narrow only the
    // `[Cond's-next-instruction, Br)` sub-range with it: a resource/
    // atomic call *there* picks up this check's own condition the same
    // way a masked load in the same sub-range already does (a masked
    // load is a fresh `createMaskedLoad` call every time `applyStageMasks`
    // walks past a still-raw `LoadInst`, so it always reflects whichever
    // mask is current *then*; a resource/image call's own mask operand,
    // once narrowed away from `SPIRVResourceLoweringPass`'s original
    // constant-`true` placeholder, is permanently stuck there by
    // `narrowCallMask`'s own "only if still constant" guard -- see its
    // comment -- so which mask is current the *first* time
    // `applyStageMasks` reaches it is the only one that will ever
    // stick). A call *before* `Cond` must still keep using the raw,
    // not-yet-check-narrowed `Masks` exactly as before: `Cond` itself can
    // depend on a value that same call just produced (e.g. a shared
    // counter's own break check, see `loop-internal-break-with-mask-op.
    // ll`), so `MasksAfterCheck`'s own defining instructions -- inserted
    // right after `Cond` -- would not yet dominate a use any earlier.
    //
    // Without this split, a real, reproduced `dEQP-VK.compute.pipeline.
    // zero_initialize_workgroup_memory.max_workgroup_memory` miscount: a
    // degenerate, single-block cycle whose `Header`/`Latch`/`CheckBlock`
    // all coincide (so this is the *only* `applyStageMasks` call that
    // ever sees a resource call here still carrying that constant
    // placeholder) ran one wasted, out-of-trip-count iteration's own
    // atomic add for every lane that was merely still live *entering*
    // the check, regardless of whether that lane's own iteration was
    // actually in range, double-counting exactly one lane per wave.
    Instruction *CheckCondInst = dyn_cast<Instruction>(CheckExit->Cond);
    BasicBlock::iterator CheckSplitPt =
        (CheckCondInst && CheckCondInst->getParent() == CheckBlock)
            ? std::next(CheckCondInst->getIterator())
            : CheckBlock->getFirstNonPHIIt();
    // Roadmap L442: `freezeRelayChainExitValues` (called earlier, for
    // `HeaderExit`'s own relay chain, when this degenerate single-block
    // cycle collapses `Header`==`CheckBlock`) may have already inserted
    // its own `select`s for this very block's phis right after
    // `CheckCondInst` (its own `IRBuilder` anchor is "this block's
    // current terminator", which at that earlier point in time was
    // exactly this position). Those selects are later relied upon
    // (by this cycle's own further restructuring, once `Header`'s own
    // relay-chain phi collapses down to a single remaining value) to
    // dominate *every* other use of `CheckExit->Cond` -- including any
    // the vanilla "insert right after `Cond`" placement below would add.
    // Walk forward past any such pre-existing `select`s (never anything
    // `applyStageMasks` itself would ever touch, so skipping them here
    // changes nothing about which calls/loads get re-masked) before
    // settling on the final split point, so `MasksAfterCheck`'s own
    // defining instructions always land *after* them instead of
    // squeezing in between the phis and that earlier freeze (confirmed,
    // via a real reproduced `Instruction does not dominate all uses!`
    // verifier failure on `loop-in-switch-divergent-header-and-relay.
    // ll`, otherwise).
    while (CheckSplitPt != CheckBlock->end() && isa<SelectInst>(*CheckSplitPt))
      ++CheckSplitPt;
    applyStageMasks(CheckBlock->begin(), CheckSplitPt, Masks);

    IRBuilder<> CheckBuilder(CheckSplitPt);
    Value *Staying = CheckExit->ExitOnTrue
                         ? CheckBuilder.CreateNot(CheckExit->Cond)
                         : CheckExit->Cond;
    // Roadmap L297: a lane transitions from live to masked-off right
    // here, specifically because of `CheckBlock`'s own condition (as
    // opposed to `Header`'s, already applied earlier into `Masks`),
    // precisely when it was still live entering this check and
    // `Staying` says to leave -- fold that into `ExitedViaCheck`'s own
    // running flag (monotonic, like `Masks.Live` itself: once true for a
    // lane, it stays true, since a lane that already exited never
    // re-enters this check again) so the restore loop below can tell
    // which of `Header`'s or `CheckBlock`'s own captured value is the
    // semantically correct one to use for this lane.
    Value *DivergedViaCheckThisIter = CheckBuilder.CreateAnd(
        Masks.Live, CheckBuilder.CreateNot(Staying),
        "exited.via.check.this.iter");
    Value *ExitedViaCheckNext = CheckBuilder.CreateOr(
        DivergedViaCheckThisIter, ExitedViaCheck, "exited.via.check.next");
    MaskPair MasksAfterCheck =
        stayInLoop(CheckBuilder, Masks, Staying, "active.check");

    applyStageMasks(CheckSplitPt, CheckBlock->end(), MasksAfterCheck);
    rethreadNestedEntryMasks(CheckBlock, MasksAfterCheck);

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
    //
    // (`ExitBlockRelayValues` itself is declared earlier now, alongside
    // `Header`'s own analogous capture when `HeaderDivergent` -- see that
    // code's own comment -- so both captures share one restore loop
    // below.)
    for (PHINode &PN : ExitBlock->phis())
      if (int Idx = PN.getBasicBlockIndex(CheckExit->RelayBlock); Idx != -1)
        ExitBlockRelayValues.emplace_back(&PN, PN.getIncomingValue(Idx),
                                          RelaySource::Check);

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
    // Roadmap L419(ac): see the identical call, and its own
    // more-thoroughly-commented reasoning, at `HeaderExit`'s own erase
    // just above.
    freezeRelayChainExitValues(CheckExit->RelayChain, CheckBlock,
                               MasksAfterCheck.Live, Header,
                               RelayCapturedHeaderPhis);
    if (llvm::is_contained(predecessors(ExitBlock), CheckBlock))
      ExitBlock->removePredecessor(CheckBlock, /*KeepOneInputPHIs=*/true);
    UncondBrInst::Create(CheckExit->StayInLoop, CheckExit->Br->getIterator());
    CheckExit->Br->eraseFromParent();

    for (BasicBlock *BB : *PostRegion) {
      applyStageMasks(*BB, MasksAfterCheck);
      rethreadNestedEntryMasks(BB, MasksAfterCheck);
    }
    // Roadmap L419(w): see the identical `PreRegion` call above.
    if (!flattenTaintedBranchesInRegion(*PostRegion, MasksAfterCheck, C))
      return false;
    applyStageMasks(*Latch, MasksAfterCheck);
    rethreadNestedEntryMasks(Latch, MasksAfterCheck);

    Value *Continue = closeLatch(Latch, Header, MasksAfterCheck);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
    // Roadmap H94a: `Latch` just became a brand-new predecessor of
    // `ExitBlock` (unlike the `HeaderDivergent`/`LatchDivergent` cases
    // below, where `Latch` already targeted `ExitBlock` directly before
    // this transform) -- restore the value each of `ExitBlock`'s own
    // leftover phis captured above for this edge too, so every one of
    // `ExitBlock`'s phis still lists exactly one entry per real
    // predecessor.
    // Roadmap L297: merge each phi's `Header`- and `Check`-sourced
    // captures (see `ExitBlockRelayValues`'s own declaration comment
    // above) rather than letting whichever happens to run first in this
    // list silently win -- a `MapVector` keeps the merge order (and
    // therefore the new `select`s' own relative order in the IR)
    // deterministic.
    MapVector<PHINode *, std::pair<Value *, Value *>> MergedRelayValues;
    for (auto &[PN, V, Source] : ExitBlockRelayValues) {
      std::pair<Value *, Value *> &Entry = MergedRelayValues[PN];
      (Source == RelaySource::Header ? Entry.first : Entry.second) = V;
    }
    IRBuilder<> RelayRestoreBuilder(Latch->getTerminator());
    for (auto &[PN, HeaderAndCheck] : MergedRelayValues) {
      if (PN->getBasicBlockIndex(Latch) != -1)
        continue;
      auto &[HeaderValue, CheckValue] = HeaderAndCheck;
      Value *Restored;
      if (HeaderValue && CheckValue && HeaderValue != CheckValue)
        Restored = RelayRestoreBuilder.CreateSelect(
            ExitedViaCheckNext, CheckValue, HeaderValue,
            PN->getName() + ".exit.merge");
      else
        Restored = CheckValue ? CheckValue : HeaderValue;
      PN->addIncoming(Restored, Latch);
    }
    // Replace (not add to) `Latch`'s already-seeded poison placeholder
    // (see `ExitedViaCheck`'s own creation comment above).
    ExitedViaCheck->setIncomingValueForBlock(Latch, ExitedViaCheckNext);
    freezeLoopCarriedValues(
        Header, Latch, MasksAfterCheck.Live,
        {cast<PHINode>(EntryMasks.Live), cast<PHINode>(EntryMasks.SideEffect),
         ExitedViaCheck},
        [&](const BasicBlock *BB) { return CI.contains(C, BB); },
        SmallVector<PHINode *, 4>(RelayCapturedHeaderPhis.begin(),
                                  RelayCapturedHeaderPhis.end()));
    addLatchIncoming(EntryMasks, MasksAfterCheck);
    // Roadmap L282: record this cycle's own entry masks so an *enclosing*
    // cycle (linearized afterward, post-order) can retroactively narrow
    // them via `rethreadNestedEntryMasks` if it turns out this cycle's
    // header sits inside a region that enclosing cycle masks too -- see
    // that function's own comment.
    HeaderActiveMasks[Header] = EntryMasks;
    return true;
    } // end DivergentCandidates-non-empty handling (Roadmap L197)
  }

  // Roadmap K6v: this branch's own body-region flattening below
  // (`collectUniformPassThroughRegion`/`DF.flattenLoopBodyRegion`) assumes
  // `Latch` itself stays a plain, unconditional pass-through throughout --
  // true whenever `Latch` already naturally owned its own real check (an
  // ordinary, non-promoted `LatchExit`, e.g. `loop-body-diamond.ll`'s own
  // `merge` block) just as much as when `Header` alone owns it, so this
  // branch must still run for both those shapes exactly as before. Only
  // the *promoted* shape (the fold above having just relocated `Latch` to
  // a block that used to be one hop further upstream -- see its own
  // comment) breaks that assumption: `Latch`'s own condition there still
  // needs the "any lane still active" treatment `closeLatch` gives it
  // below, which only the general combined handling further down
  // (reached whenever `LatchDivergent` is true) provides. Skip this
  // branch only for the promoted shape so it falls through to that
  // handling instead, exactly as if the promoted check were a divergent
  // one needing the same structural treatment (confirmed via a real
  // regression against `loop-body-diamond.ll` when this guard was
  // initially written as `!LatchExit` instead of `!LatchWasPromoted`,
  // wrongly also catching the ordinary non-promoted "Latch already owns
  // the check" shape).
  if (!HeaderDivergent && !LatchDivergent && !LatchWasPromoted) {
    // Roadmap L292: a loop whose own header/latch exit check is itself
    // uniform is ordinarily left alone below (nothing to linearize about
    // its own *iteration* decision) -- but its body can still contain a
    // `feme.stage.discard`/`.demote`/other mask-affecting op (directly, or
    // inside a plain uniform internal diamond -- see `dEQP-VK.glsl.demote.
    // dynamic_loop_always`'s own `if (i > 0) demote`) whose effect must
    // still accumulate across iterations via a loop-carried mask, exactly
    // as a genuinely divergent loop's body already does below. Detect
    // that need here (scoped to this cycle's own blocks, not the whole
    // function -- an unrelated mask op elsewhere is `DiamondFlattener`'s
    // or a different cycle's problem) before falling through to the
    // ordinary "nothing to do" bail.
    bool CycleHasMaskOps = false;
    for (BasicBlock &BB : F) {
      if (!CI.contains(C, &BB))
        continue;
      for (Instruction &I : BB) {
        auto *Call = dyn_cast<CallInst>(&I);
        if (!Call)
          continue;
        feme::StageOpKind Kind;
        if (feme::isStageOpCall(*Call, &Kind) &&
            (Kind == feme::StageOpKind::Discard ||
             Kind == feme::StageOpKind::Demote ||
             Kind == feme::StageOpKind::IsHelper ||
             Kind == feme::StageOpKind::OutputStore ||
             Kind == feme::StageOpKind::StreamEmit ||
             Kind == feme::StageOpKind::StreamCut ||
             Kind == feme::StageOpKind::TaskPayloadStore ||
             Kind == feme::StageOpKind::SetMeshOutputs ||
             Kind == feme::StageOpKind::EmitMeshTasks)) {
          CycleHasMaskOps = true;
          break;
        }
        // Roadmap K6s: a `feme.cpu.resource.*`/`feme.cpu.image.*` call's
        // own trailing mask operand starts out as the compile-time
        // constant `true` `SPIRVResourceLoweringPass`/`lowerImageAccesses`
        // gives every such call, relying entirely on this pass (via
        // `applyStageMasks`'s own `matchResourceCall`/`matchImageCall`
        // handling) to narrow it to the real per-branch predicate. A
        // *uniform* loop reached only through an enclosing divergent
        // diamond's arm (that diamond's own `DiamondFlattener::flatten`
        // stops at this cycle's boundary and defers to this pass -- see
        // its own comment) previously fell through the "no divergence, no
        // mask ops" bail below whenever its body's only mask-needing
        // operations were plain resource/image calls, not one of the
        // `StageOpKind`s above -- leaving every one of those calls'
        // masks stuck at `true` and letting the whole loop body execute
        // unconditionally for every invocation, not just the one(s) the
        // enclosing diamond's condition actually selected (found reducing
        // `dEQP-VK.spirv_assembly.instruction.graphics.variable_pointers.
        // graphics.writes_single_buffer_frag`'s own uniformly-offset
        // buffer-readback mismatch to this exact gap).
        if (matchResourceCall(*Call) || matchImageCall(*Call)) {
          CycleHasMaskOps = true;
          break;
        }
      }
      if (CycleHasMaskOps)
        break;
    }
    if (!CycleHasMaskOps && !HasDeferredDivergentCandidates)
      return false; // No divergence, no mask ops: a real uniform loop,
                     // left alone.

    // Roadmap L292: `HeaderExit` (computed above) already applies
    // `recoverRelayExitCheck`'s own structural fallback when `Header`'s
    // own branch is the loop's real, uniform trip-count check but
    // `StructurizeCFG` routed both its real "continue" and real "exit"
    // edges through a single shared `Flow`-style dispatch block -- see
    // that function's own comment for why this recovery, rather than
    // `matchExitCheckWithRelay`, is what is safe here.
    BasicBlock *HeaderStayInLoop = HeaderExit ? HeaderExit->StayInLoop : nullptr;
    if (!HeaderStayInLoop) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has a mask-affecting operation in its body but no "
                      "recognized uniform exit check; unsupported (roadmap "
                      "milestone 6 deviation)");
      return false;
    }

    // Roadmap L292: confirmed via a real, reproduced `verifyModule`
    // failure -- check the region *before* mutating anything at `Header`
    // below (see `canFlattenLoopBodyRegion`'s own comment): a failure
    // discovered only after `makeActivePNPair`/`applyStageMasks` already
    // added a loop-carried mask phi to `Header` would otherwise leave that
    // phi stranded, with no corresponding backedge value ever added (this
    // whole cycle is about to be diagnosed and left alone, never reaching
    // `addLatchIncoming` below), breaking module verification.
    //
    // Roadmap K6z: lets both the precheck below and the real
    // `flattenLoopBodyRegion` call walk straight through a nested sibling
    // cycle entirely contained in this cycle's own body -- see
    // `DiamondFlattener`'s own constructor parameter comment. Any such
    // child (`CI.contains(C, ChildHeader)`, and not `C` itself) is
    // *always* already fully attempted by the time this cycle's own
    // `linearizeCycle` call runs, regardless of whether it ended up
    // actually masked (an entry in `HeaderActiveMasks`) or left
    // completely alone (no mask-affecting op of its own anywhere in its
    // body, `linearizeCycle` returning false for it, see the
    // `!CycleHasMaskOps && !HasDeferredDivergentCandidates` bail above) --
    // `linearizeCyclePostOrder`'s own post-order guarantees children are
    // always visited before any parent, unconditionally, not just the
    // ones that turned out to need masking (confirmed the hard way: an
    // earlier version of this fix gated on `HeaderActiveMasks.count`
    // instead, which silently still rejected `matrix_single_subscript_
    // load.test`'s own nested `for` loop -- itself mask-op-free, only the
    // *outer* loop's own diamond-chain body needs any masking at all).
    // `rethreadNestedEntryMasks` (called by `flatten`, never `validate`,
    // via `NarrowChildEntryMasks` below) is still always safe to call
    // either way: it is already a no-op for a `ChildHeader` with no
    // `HeaderActiveMasks` entry (nothing to narrow -- no entry-mask phi
    // was ever created for a loop left alone), so this fix only *adds*
    // the narrowing where there is anything to narrow at all, never
    // narrows something that should not be. `getExitBlocks` is this
    // class's own mutation-safe cache (see its own comment for the
    // use-after-free/staleness crashes a live `CI.getExitBlocks` call, or
    // a naively precomputed cache of it, caused here previously) -- never
    // a fresh, unsafe call made directly against the (by now partially
    // mutated) live `CI`.
    auto GetAlreadyLinearizedCycleExit =
        [&](BasicBlock *ChildHeader) -> BasicBlock * {
      CycleRef ChildCycle = CI.getCycle(ChildHeader);
      if (!ChildCycle.isValid() || ChildCycle == C ||
          !CI.contains(C, ChildHeader))
        return nullptr;
      ArrayRef<BasicBlock *> Exits = getExitBlocks(ChildCycle);
      SmallPtrSet<BasicBlock *, 2> UniqueExits(Exits.begin(), Exits.end());
      if (UniqueExits.size() != 1)
        return nullptr; // Multiple exits: not yet supported here.
      return *UniqueExits.begin();
    };
    {
      // Roadmap L375 (6th investigation): deliberately a fresh, local set,
      // not the real `MergeVisited` shared with the whole rest of this
      // function's processing -- this is a read-only feasibility probe
      // before any mutation below, and the real, authoritative check is
      // `DF.flattenLoopBodyRegion`'s own call just below, which *does*
      // share `MergeVisited` and is the one that actually decides whether
      // to proceed. If this precheck optimistically passes but the real
      // call then disagrees (because some other, already-processed
      // cycle's own candidate claimed a block this region also reaches),
      // the `if (!MasksAtLatch)` diagnostic below still catches it
      // cleanly -- no worse than this precheck not existing at all for
      // that one case.
      SmallPtrSet<BasicBlock *, 16> PrecheckMergeVisited;
      SmallPtrSet<BasicBlock *, 16> PrecheckResolvedBlocks;
      DiamondFlattener Precheck(F, DT, PDT, CI, UI, PrecheckMergeVisited,
                                PrecheckResolvedBlocks,
                                GetAlreadyLinearizedCycleExit);
      bool PrecheckResult =
          Precheck.canFlattenLoopBodyRegion(HeaderStayInLoop, Latch);
      if (!PrecheckResult) {
        diagnose(F, "loop at '" + Header->getName() +
                        "' has an internal branch in a body shape this "
                        "milestone does not yet lower (e.g. an empty "
                        "diamond arm, or one crossing another cycle's own "
                        "boundary); unsupported (roadmap milestone 6 "
                        "deviation)");
        return false;
      }
    }

    MaskPair Masks = makeActivePNPair();
    applyStageMasks(*Header, Masks);
    rethreadNestedEntryMasks(Header, Masks);

    // Roadmap K6z: this must be a named local, not an unnamed temporary
    // passed directly as `DF`'s constructor argument below -- `DF` only
    // stores a `function_ref` (a non-owning pointer to the callable), so
    // an unnamed temporary lambda would be destroyed at the end of the
    // constructor call's own full-expression, leaving `DF.
    // NarrowChildEntryMasks` dangling the first time `flatten()` actually
    // invokes it (confirmed the hard way: a SEGV inside
    // `rethreadNestedEntryMasks` with a garbage `this`). Giving it the
    // same name/scope as `DF` itself (both declared in this block, `DF`
    // destroyed first since it is declared second) keeps it alive for the
    // whole of `DF`'s lifetime.
    auto NarrowChildEntryMasks = [&](BasicBlock *ChildHeader,
                                      const MaskPair &ChildMasks) {
      rethreadNestedEntryMasks(ChildHeader, ChildMasks);
    };
    DiamondFlattener DF(F, DT, PDT, CI, UI, MergeVisited, ResolvedBlocks,
                        GetAlreadyLinearizedCycleExit, NarrowChildEntryMasks);
    std::optional<MaskPair> MasksAtLatch =
        DF.flattenLoopBodyRegion(HeaderStayInLoop, Latch, Masks);
    if (!MasksAtLatch) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in a body shape this "
                      "milestone does not yet lower (e.g. an empty diamond "
                      "arm, or one crossing another cycle's own boundary); "
                      "unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    applyStageMasks(*Latch, *MasksAtLatch);

    freezeLoopCarriedValues(
        Header, Latch, MasksAtLatch->Live,
        {cast<PHINode>(Masks.Live), cast<PHINode>(Masks.SideEffect)},
        [&](const BasicBlock *BB) { return CI.contains(C, BB); });
    addLatchIncoming(Masks, *MasksAtLatch);
    // Roadmap L292: `Header`'s own exit edge to `ExitBlock` is untouched
    // by this path (unlike the `HeaderDivergent` case below, this check
    // really is uniform, so every lane takes it together -- no
    // restructuring into a latch-only exit is needed for correctness of
    // the *iteration* decision itself). But `DiamondFlattener::run()`
    // already walked `ExitBlock` as one of its own "cycle exit" roots
    // *before* this pass ever ran (see its own comment on `Roots`),
    // seeding that walk with a guess (`CycleBoundaryMasks[Header]`, the
    // mask reaching `Header` from *outside* the loop, i.e. the loop's own
    // entry mask, not its *exit* mask) that is only ever correct when
    // `Header`'s own mask-affecting op decision cannot differ between
    // entry and exit -- false here, precisely because the loop's body
    // (just flattened above) can have narrowed `Masks`'s `SideEffect`
    // field via a `demote` since entry. That stale guess already baked a
    // wrong `feme.cpu.stage.return.masks`/masked-output-store call into
    // `ExitBlock` using the wrong, pre-loop mask (reduced from
    // `dEQP-VK.glsl.demote.dynamic_loop_always`'s own residual image
    // mismatch after the rest of this path was otherwise working).
    // Re-running `applyStageMasks` here with the real, now-fully-updated
    // `Masks` overrides it: `ExitBlock` is reached directly from `Header`
    // (this uniform check's own `StructurizeCFG`-relay target), so
    // `Header`'s own accumulated `Masks` -- unchanged across that
    // specific edge, since a genuinely uniform branch narrows nothing --
    // is exactly the mask every lane actually carries there.
    applyStageMasks(*ExitBlock, Masks);
    rethreadNestedEntryMasks(ExitBlock, Masks);
    // Roadmap L282: see the identical comment at the other two success
    // paths' own `return true`.
    HeaderActiveMasks[Header] = Masks;
    return true;
  }

  MaskPair Masks = makeActivePNPair();
  applyStageMasks(*Header, Masks);
  rethreadNestedEntryMasks(Header, Masks);
  MaskPair MasksAtLatch = Masks;
  // Roadmap L292: `HeaderExit`'s own match may be via a relay (see
  // `ExitCheck::RelayBlock`'s own comment, and `recoverRelayExitCheck`)
  // rather than a direct edge from `Header` itself to `ExitBlock` --
  // capture/restore any of `ExitBlock`'s own leftover phi values the
  // same way the `CheckBlock` case above does (Roadmap H94a), keyed by
  // `RelayBlock` (which is simply `Header` itself for a direct match,
  // making this a no-op change for that, previously-only-supported,
  // case).
  SmallVector<std::pair<PHINode *, Value *>, 4> HeaderExitRelayValues;
  // Roadmap L419(ac): see the identical declaration's own comment in the
  // `DivergentCandidates` branch above.
  SmallPtrSet<PHINode *, 4> RelayCapturedHeaderPhis;
  // Roadmap L298: this shape's own region between `Header`'s own exit
  // check and `Latch` is required to be a uniform pass-through in the
  // *control-flow* sense `collectUniformPassThroughRegion` already
  // checks (no second genuinely divergent branch anywhere in it -- a
  // `DivergentCandidates`-non-empty cycle takes the branch above
  // instead) -- but it is **not** required to be free of mask-affecting
  // *calls*: a `feme.stage.discard`/`.demote`/other `StageOpKind`, or a
  // convergent wave intrinsic (`WaveActiveBitAnd`/`Ballot`/a reduce/
  // scan/...), sitting directly in this region (not inside some nested
  // child cycle's own already-linearized interior, which
  // `rethreadNestedEntryMasks` alone correctly handles below) still
  // needs exactly the same `Masks.Live`/`.SideEffect` narrowing
  // `applyStageMasks` gives the `DivergentCandidates` branch's own
  // `PreRegion`/`PostRegion` above -- a prior version of this comment
  // claimed "this shape would have been rejected already" for such a
  // call, which was never actually true: no earlier check in this
  // function scans for one. Confirmed via a real, reproduced
  // `Basic/Mandelbrot.test`-adjacent repro: `WaveActiveBitAnd` inside a
  // loop whose own header check is itself the loop's *only* divergent
  // decision (no separate `CheckBlock`, so `DivergentCandidates` is
  // empty) reached its lowered `llvm.spv.wave.reduce.and.i32` call with
  // no masking select at all, ANDing in every lane's value unconditionally
  // regardless of which lanes had actually finished iterating already
  // (`WaveActiveBitAnd.convergence.test`'s own divergent-trip-count-loop
  // sub-case). `collectUniformPassThroughRegion` doubles as the missing
  // validation step here too: if the region is not actually a uniform
  // pass-through, it returns `std::nullopt` and this cycle is correctly
  // diagnosed and left alone instead of silently mis-masked.
  //
  // This is computed *before* `Header`'s own exit branch is mutated below
  // (unlike the `DivergentCandidates` branch above, which only ever needs
  // it afterwards) specifically so the "shared relay block" check right
  // after it can still see `HeaderExit->RelayBlock`'s real, pre-mutation
  // predecessor set -- see that check's own comment for why.
  BasicBlock *BodyRegionStart =
      HeaderDivergent ? HeaderExit->StayInLoop : Header;
  std::optional<SmallPtrSet<BasicBlock *, 8>> BodyRegion =
      collectUniformPassThroughRegion(BodyRegionStart, Latch, ExitBlock, C,
                                       PeeledFrom);
  if (!BodyRegion) {
    diagnose(F, "loop at '" + Header->getName() +
                    "' has an internal branch in a body shape this "
                    "milestone does not yet lower; only a uniform "
                    "pass-through region from the header's own exit check "
                    "to the latch is supported yet (roadmap milestone 6 "
                    "deviation)");
    return false;
  }
  if (HeaderDivergent) {
    IRBuilder<> B(HeaderExit->Br);
    Value *Cond = HeaderExit->Cond;
    Value *Staying = HeaderExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    MasksAtLatch = stayInLoop(B, Masks, Staying, "active.header");
    // Never really exit here: always continue toward the latch, letting an
    // inactive lane's iterations become no-ops instead (see the file
    // comment above).
    // Roadmap L374: capture the leftover value from `HeaderExit->RelayBlock`
    // (the only place it currently exists -- for a direct match
    // (`matchExitCheck`), `RelayBlock == Header`; for a genuine relay
    // (`recoverRelayExitCheck`), it is instead the relay chain's own last
    // hop, which alone is ExitBlock's real, listed predecessor), but only
    // manually strip *`Header`'s own* entry below, not `RelayBlock`'s --
    // matching `CheckBlock`'s own, already-correct capture-via-relay/
    // remove-via-self split above (see its identical comment). `Header`
    // itself is the block whose own terminator (`HeaderExit->Br`) is
    // actually being erased here, so it is the only one guaranteed to
    // need its `ExitBlock` entry fixed up *right now*: in the direct-match
    // case that is `RelayBlock` itself (no behavior change from before);
    // in the relay case, `Header` was never literally one of `ExitBlock`'s
    // own predecessors to begin with (`is_contained` below is false, a
    // clean no-op), and `RelayBlock`'s own edge to `ExitBlock` is left
    // completely untouched for now -- it becomes genuinely unreachable as
    // a side effect of erasing `HeaderExit->Br` below (the relay chain's
    // only path in vanishes with it), and our caller's own
    // `EliminateUnreachableBlocks(F)` cleans it up correctly afterwards,
    // including fixing up this same `ExitBlock` phi for it then. Removing
    // `RelayBlock`'s entry *here* too (the previous, buggy behavior)
    // stripped a *still-live* predecessor's phi entry out from under it
    // whenever that same relay block was *also* reachable independently
    // from elsewhere in the loop body (confirmed via a real, reproduced
    // `PHINode::removeIncomingValue` assertion inside
    // `EliminateUnreachableBlocks`, from `dEQP-VK.spirv_assembly.
    // instruction.graphics.loop.break_frag`): `StructurizeCFG` can route
    // the loop body's own separate uniform "hard iteration bound" check
    // through the very same relay block `Header`'s own divergent check
    // also relays through, leaving that block (and its real edge to
    // `ExitBlock`) alive in the CFG long after this premature strip
    // already told `ExitBlock`'s phis it was gone -- a desync
    // `EliminateUnreachableBlocks`'s own later, correct removal attempt
    // for the same (by-then genuinely dead) block then crashes on,
    // finding no entry left to remove.
    for (PHINode &PN : ExitBlock->phis())
      if (int Idx = PN.getBasicBlockIndex(HeaderExit->RelayBlock); Idx != -1)
        HeaderExitRelayValues.emplace_back(&PN, PN.getIncomingValue(Idx));
    // Roadmap L419(ac): see `freezeRelayChainExitValues`'s own comment;
    // must run before `HeaderExit->Br` below is erased.
    freezeRelayChainExitValues(HeaderExit->RelayChain, Header,
                               MasksAtLatch.Live, Header,
                               RelayCapturedHeaderPhis);
    if (llvm::is_contained(predecessors(ExitBlock), Header))
      ExitBlock->removePredecessor(Header, /*KeepOneInputPHIs=*/true);
    UncondBrInst::Create(HeaderExit->StayInLoop, HeaderExit->Br->getIterator());
    HeaderExit->Br->eraseFromParent();
  }

  // A *child* cycle's own header can still legitimately sit anywhere in
  // this pass-through region (e.g. the `dowhile_trap` shape's own inner
  // `do`-`while`, entirely between this outer loop's own header check
  // and its latch): `applyStageMasks` itself is safe to call on such a
  // header/latch too (its mask-affecting-call scan simply finds nothing
  // there to narrow, since a linearized child's own calls were already
  // narrowed by its own, further-restricted masks), but the child's own
  // entry mask still needs retroactively narrowing by `MasksAtLatch`
  // (this cycle's own per-lane "is this iteration of my own body still
  // wanted" decision) via `rethreadNestedEntryMasks` exactly as it would
  // in the `DivergentCandidates` branch -- the `dowhile_trap` bug's own
  // repro (`dEQP-VK.glsl.loops.special.for_dynamic_iterations.
  // dowhile_trap_fragment`) hung until that rethreading was added here,
  // because that shape actually takes *this* branch, not
  // `DivergentCandidates`.
  for (BasicBlock *BB : *BodyRegion) {
    applyStageMasks(*BB, MasksAtLatch);
    rethreadNestedEntryMasks(BB, MasksAtLatch);
  }

  applyStageMasks(*Latch, MasksAtLatch);
  rethreadNestedEntryMasks(Latch, MasksAtLatch);
  MaskPair MasksAfterLatchCheck = MasksAtLatch;
  Value *Continue;
  if (LatchDivergent) {
    IRBuilder<> B(LatchExit->Br);
    Value *Cond = LatchExit->Cond;
    Value *Staying = LatchExit->ExitOnTrue ? B.CreateNot(Cond) : Cond;
    MasksAfterLatchCheck = stayInLoop(B, MasksAtLatch, Staying, "active.latch");
    Continue = createUniformMaskAny(B, MasksAfterLatchCheck.Live, "loop.continue");
    SmallVector<std::pair<PHINode *, Value *>, 4> LatchExitRelayValues;
    // Roadmap L374: capture from `LatchExit->RelayBlock` (the only place
    // the value currently lives, whether direct match or relay -- see
    // the identical `HeaderExit` fix above for the full reasoning), but
    // only manually strip `Latch`'s *own* entry below. `Latch` always
    // gains a brand-new edge to `ExitBlock` just below (`CondBrInst::
    // Create`, unconditionally, regardless of this branch) and so always
    // needs *some* phi value for it: in the direct-match case
    // (`RelayBlock == Latch`) that is its own existing entry, captured
    // and then immediately restored below (net no-op); in the relay case
    // `Latch` was never literally a predecessor of `ExitBlock` before
    // (`is_contained` below is false, a clean no-op here), so the value
    // captured from `RelayBlock` is instead what gets attached to
    // `Latch`'s new edge, by the restore loop below -- `RelayBlock`'s own
    // (eventually dead) entry is left for `EliminateUnreachableBlocks` to
    // clean up on its own, exactly as `HeaderExit`'s fix does.
    for (PHINode &PN : ExitBlock->phis())
      if (int Idx = PN.getBasicBlockIndex(LatchExit->RelayBlock); Idx != -1)
        LatchExitRelayValues.emplace_back(&PN, PN.getIncomingValue(Idx));
    // Roadmap L419(ac): see `freezeRelayChainExitValues`'s own comment;
    // must run before `LatchExit->Br` below is erased.
    freezeRelayChainExitValues(LatchExit->RelayChain, Latch,
                               MasksAfterLatchCheck.Live, Header,
                               RelayCapturedHeaderPhis);
    if (llvm::is_contained(predecessors(ExitBlock), Latch))
      ExitBlock->removePredecessor(Latch, /*KeepOneInputPHIs=*/true);
    CondBrInst::Create(Continue, Header, ExitBlock,
                       LatchExit->Br->getIterator());
    LatchExit->Br->eraseFromParent();
    for (auto &[PN, V] : LatchExitRelayValues)
      if (PN->getBasicBlockIndex(Latch) == -1)
        PN->addIncoming(V, Latch);
  } else {
    // The latch's own condition (if any) is real/uniform control flow and
    // stays exactly as it branches today, conjoined with "any lane still
    // active" so a uniform-false natural exit still wins, and so a
    // uniform-true natural continue does not resurrect a lane the header's
    // divergent check already deactivated.
    Continue = closeLatch(Latch, Header, MasksAtLatch);
    CondBrInst::Create(Continue, Header, ExitBlock, Latch);
  }
  // Roadmap H94a/L292: `Latch` just became (or already was) a predecessor
  // of `ExitBlock` via the `CondBrInst::Create` just above -- restore
  // whatever value `HeaderExit->RelayBlock` itself used to contribute to
  // any of `ExitBlock`'s own leftover phis (captured before that
  // predecessor was removed, above), so every one of `ExitBlock`'s phis
  // still lists exactly one entry per real predecessor. A no-op whenever
  // `HeaderExit` was a direct (non-relay) match or `HeaderDivergent` is
  // false, since then either `HeaderExitRelayValues` is empty or `Latch`
  // already had its own, unrelated incoming value for these phis.
  for (auto &[PN, V] : HeaderExitRelayValues)
    if (PN->getBasicBlockIndex(Latch) == -1)
      PN->addIncoming(V, Latch);

  freezeLoopCarriedValues(
      Header, Latch, MasksAtLatch.Live,
      {cast<PHINode>(Masks.Live), cast<PHINode>(Masks.SideEffect)},
      [&](const BasicBlock *BB) { return CI.contains(C, BB); },
      SmallVector<PHINode *, 4>(RelayCapturedHeaderPhis.begin(),
                                RelayCapturedHeaderPhis.end()));
  addLatchIncoming(Masks, MasksAfterLatchCheck);
  // Roadmap L282: see the identical comment at the `DivergentCandidates`
  // branch's own `return true` above.
  HeaderActiveMasks[Header] = Masks;
  return true;
}

bool LoopLinearizer::run() {
  precomputeExitBlocks();
  bool Changed = false;
  for (CycleRef C : CI.toplevel_cycles())
    Changed |= linearizeCyclePostOrder(C);
  return Changed;
}

bool LoopLinearizer::linearizeCyclePostOrder(CycleRef C) {
  // Roadmap L197: recurses post-order (every descendant fully attempted
  // before `C` itself would be), and `C` itself is designed to be
  // attempted here too once safe -- see the hazard comment below for
  // exactly why that last step is not yet enabled. The reasoning in this
  // comment (about why a linearized child's own interior is safe for an
  // enclosing cycle's own classification logic to treat as ordinary
  // uniform pass-through content) is preserved for whenever that step is
  // turned on: `linearizeCycle(C)`, if it did attempt a non-leaf `C`,
  // would trust an already-processed child's own interior to already
  // look like an ordinary uniform pass-through region to
  // `collectUniformPassThroughRegion`'s own scan: a linearized child's
  // `Header`/`Latch` still end in a real `CondBrInst` (its backedge is
  // never removed, only ever made unconditional on one arm when a
  // divergent header/latch check is converted -- see `closeLatch`), but
  // that `CondBr`'s own condition is always either a genuinely uniform,
  // untouched original value or one this pass itself already recorded in
  // `KnownUniformValues` (a `feme.cpu.mask.any` reduction, or one of
  // `closeLatch`'s own uniform-operand compositions) -- never a stale
  // answer from `UI`, thanks to `isDivergentBranch` consulting
  // `KnownUniformValues` first (see that member's own comment). A child
  // left unsupported (still containing a genuinely divergent shape this
  // milestone's classification cannot handle, or one this pass simply
  // declined to touch) would likewise be safe to leave alone: `C`'s own
  // scan would still see whatever divergent `CondBr` that child's own
  // failed attempt left behind, and correctly diagnose or decline `C`
  // itself the same way it always has for an unsupported interior branch.
  // Roadmap L197: `DT`/`PDT` are, like `CI`'s own frozen `ExitBlocks`
  // cache this milestone's own `getExitBlocks` accessor was added to
  // sidestep, invalidated by a child cycle's own block erasures
  // (`foldRedundantFlowBlocksInCycle`/`mergeTrivialRelayBlocksInCycle`)
  // -- unlike that cache, though, there is no way to precompute every
  // cycle's own dominance answer up front, since `linearizeCycle`'s own
  // interior `DiamondFlattener` (used for its "flatten loop body
  // diamond" fixed point below) genuinely needs live, correct dominance
  // over whatever the CFG currently looks like at the point it runs, not
  // a snapshot from before any earlier sibling/child was linearized.
  // Unlike `UniformityInfo` (see the `UI` member's own comment, and
  // historical commit b9cba5d890f3), a plain structural recompute here
  // is always sound: `DominatorTree`/`PostDominatorTree` encode no
  // per-value uniformity judgment to go stale, only genuine CFG shape,
  // so refreshing them in place via `recalculate` against the current,
  // fully-linked (if not yet fully linearized) CFG is exactly correct,
  // not merely tolerated. Cheap enough next to getting this wrong to do
  // unconditionally, once per cycle, rather than trying to track exactly
  // which children actually mutated anything.
  //
  // Roadmap L197/L198/L200/L282: attempting `C` itself here (not just its
  // children) needed six distinct bugs fixed, found via real Vulkan CTS
  // shaders once a non-leaf `C` was actually attempted (all six now
  // fixed):
  //  1. `DiamondFlattener::isLoopControlEdge` misclassifying a genuine
  //     loop-exit edge once `CI`'s own exit-block accounting (live or
  //     precomputed) went stale against blocks a child's own
  //     linearization erased or newly inserted -- fixed (see that
  //     method's own comment): it no longer consults any exit-block list
  //     at all, just a live `CI.contains` membership check.
  //  2. `DiamondFlattener::flattenLoopBodyDiamond` "succeeding" (return
  //     non-null) without actually removing `Start`'s own divergent-
  //     looking branch, because `flatten`'s own, stricter notion of
  //     "divergent" (`UI.isDivergentTerminator` plus masked-load taint)
  //     can disagree with `LoopLinearizer::isDivergentBranch`'s broader,
  //     `KnownUniformValues`-aware one that decided to attempt it in the
  //     first place -- fixed (see that method's own comment): it now
  //     checks whether `Start`'s terminator was actually replaced before
  //     reporting success.
  //  3. `DiamondFlattener::validate`'s own `R->hasNPredecessors(2)` check
  //     (`R` being a divergent branch's reconvergence point, from
  //     `immediatePostDom`, i.e. from `PDT`) could hit a real
  //     `llvm::Value::materialized_user_begin` assertion (`hasUseList()`
  //     failed) on a dangling `BasicBlock*` whose use list had already
  //     been torn down. Root cause: `linearizeCycle`'s own fold/peel/merge
  //     fixed-point logic (`foldRedundantFlowBlocksInCycle`/
  //     `peelConstantFlowPredecessorsInCycle`/
  //     `collapseTriviallyRedundantPhisInCycle`/
  //     `mergeTrivialRelayBlocksInCycle`) genuinely `eraseFromParent()`s
  //     blocks -- but it runs *after* the one `DT`/`PDT` recalculation at
  //     this function's own top, and *before* `DiamondFlattener` (which
  //     also uses that same `DT`/`PDT`) is constructed a bit further down
  //     in the same `linearizeCycle(C)` call -- so a fresh-at-entry `PDT`
  //     could still be stale by the time `DiamondFlattener` used it, once
  //     `C` was a non-leaf whose body those folds actually mutated. Fixed
  //     by adding a second `DT.recalculate(F)`/`PDT.recalculate(F)` pair
  //     immediately before `DiamondFlattener DF(...)` is constructed
  //     inside `linearizeCycle` itself (see that call site's own comment)
  //     -- `flatten()` itself never erases a whole `BasicBlock` (only
  //     instructions within one), so nothing after that point needs a
  //     third recompute.
  //  4. Once bugs 1-3 above were fixed, a full `dEQP-VK.graphicsfuzz.*`
  //     sweep hit a different, real crash on `dEQP-VK.graphicsfuzz.cov-
  //     nested-loop-large-array-index-using-vector-components`:
  //     `llvm/lib/Transforms/Utils/LCSSA.cpp`'s own `formLCSSAImpl`
  //     asserted `L.isLCSSAForm(DT)` -- some value this pass left behind,
  //     defined inside a loop and used outside it, was not a proper LCSSA
  //     `phi` at that loop's exit by the time a later pass called
  //     `formLCSSA` on it. Root cause, isolated via a standalone
  //     `feme-opt --llvm -passes=feme-cpu-linearize` repro of the same
  //     shader's own pre-linearize IR (bypassing `formLCSSA`'s own
  //     much-later assertion in favor of the plain, immediate
  //     `verifyModule` dominance check `feme-opt` already runs): inside
  //     `peelConstantFlowPredecessors`, redirecting one of `BB`'s
  //     constant-valued predecessors to bypass `BB` outright repairs every
  //     downstream use of `BB`'s own `phi`s (via `SSAUpdater`, see that
  //     loop's own comment) but left any *ordinary*, non-`phi` instruction
  //     defined in `BB` with a use outside it (e.g. a `%.inv = xor i1
  //     <phi>, true` consuming one of `BB`'s own condition `phi`s, itself
  //     further consumed downstream through more merges) completely
  //     unrepaired -- once the peel added a second, bypassing predecessor
  //     edge into one of `BB`'s descendants, `BB` no longer necessarily
  //     dominated that instruction's own use, corrupting the IR. Fixed by
  //     having `peelConstantFlowPredecessors` bail out entirely (leaving
  //     `BB` untouched) whenever it finds such an escaping non-`phi`
  //     value, rather than only ever guarding `phi`s (see that function's
  //     own comment for the full writeup).
  //  5. With bug 4 fixed and non-leaf traversal temporarily re-enabled
  //     for a full sweep, `dEQP-VK.graphicsfuzz.increment-value-in-
  //     nested-for-loop` genuinely crashed (a real `SIGSEGV`, confirmed
  //     via `gdb`, not an assertion): stack-overflowing tens of thousands
  //     of frames deep inside `DiamondFlattener::validate`'s own
  //     recursive-descent (`validate(T, R, Quiet) || validate(Fsucc, R,
  //     Quiet)`) calls, immediately after printing this exact function's
  //     own "divergent branch in 'loop.exit.guard' has no reconvergence
  //     point" diagnostic -- meaning some genuinely-nested shape drives
  //     `validate` into runaway recursion rather than the bounded
  //     diamond-nesting depth it is supposed to have, on a walk that (per
  //     the printed diagnostic) was already headed for a clean,
  //     non-crashing failure once it actually reached the top of that
  //     recursion. The precise structural cause of the runaway recursion
  //     itself was never isolated (a real, `isInCycle`/`isLoopControlEdge`
  //     staleness issue like bug 1's was suspected but not confirmed);
  //     fixed defensively instead, since a categorical bound is safe
  //     regardless of the specific cause: `DiamondFlattener::validate`
  //     now takes a `Depth` parameter (see its own declaration comment)
  //     and bails out with an ordinary diagnostic, rather than recursing
  //     further, past a generous fixed depth no real shader's diamond
  //     nesting could plausibly reach.
  //  6. With bug 5's crash converted to a clean diagnostic, a *different*,
  //     non-crashing failure mode surfaced on this milestone's own
  //     original motivating repro,
  //     `dEQP-VK.glsl.loops.special.for_dynamic_iterations.
  //     dowhile_trap_fragment` (and its 29 sibling `loops.special.
  //     *_dynamic_iterations` cases -- see the roadmap `L282` entry):
  //     pipeline creation now succeeded, but the test hung at runtime
  //     (`vk.waitForFences(...) VK_TIMEOUT`) -- confirmed via a live `gdb`
  //     attach to be a real, 100%-CPU-pinned infinite loop in the JIT'd
  //     code's own vectorized `feme.cpu.mask.any`-reduction loop for the
  //     *inner* (child) cycle, not a deadlock. Root cause: a child
  //     cycle's own `makeActivePNPair` phi, built while the child was
  //     still a leaf (before any enclosing cycle was attempted), has no
  //     way to know at that time whether every edge entering it from
  //     outside its own cycle is actually still "live" by some
  //     *enclosing* cycle's own, not-yet-computed per-lane mask -- the
  //     only sound default available then is the unconditional `true`
  //     this pass's masked-execution convention (the outer cycle always
  //     structurally enters its own body, deferring actual masking to
  //     side effects/exits) already relies on elsewhere. For a lane the
  //     *enclosing* cycle's own check should already have deactivated
  //     (e.g. a per-lane outer trip count of zero: this lane should never
  //     run the loop body, hence never run the inner `do`-`while` trap
  //     loop, at all), entering the child cycle marked unconditionally
  //     "active" anyway is only harmless if the child's own exit
  //     condition independently becomes false for that lane on its own --
  //     which it does not for this shape (the inner `do`-`while`'s own
  //     `i >= 3` check depends only on the *outer* loop's shared
  //     induction variable, itself frozen forever at its initial value
  //     for a lane that can never make outer-loop progress because it
  //     never exits the inner loop to begin with), hanging the whole
  //     wave's `mask.any` reduction forever. Fixed by `HeaderActiveMasks`/
  //     `rethreadNestedEntryMasks` (see their own comments): whenever an
  //     enclosing cycle threads its own freshly computed `MaskPair`
  //     through a region containing a previously linearized child's
  //     header, that child's `true`-constant entry edges are
  //     retroactively narrowed to the enclosing cycle's own mask instead.
  // With all six fixed and confirmed (see `docs/VulkanCTSReport.md`'s
  // `L282` entry for the before/after CTS numbers), non-leaf traversal is
  // unconditional below.
  bool Changed = false;
  for (CycleRef Child : CI.children(C))
    Changed |= linearizeCyclePostOrder(Child);
  DT.recalculate(F);
  PDT.recalculate(F);
  Changed |= linearizeCycle(C);
  return Changed;
}

void LoopLinearizer::precomputeExitBlocks() {
  // Roadmap L197: every cycle, not just leaves -- see `ExitBlocksByCycle`'s
  // own comment for why a not-yet-processed parent cycle needs its own
  // answer computed here too, before any child is linearized (and,
  // potentially, has some of its own blocks erased).
  for (CycleRef C : CI.cycles()) {
    SmallVector<BasicBlock *, 2> Exits;
    CI.getExitBlocks(C, Exits);
    ExitBlocksByCycle.try_emplace(C, std::move(Exits));
  }
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

    DenseMap<BasicBlock *, WeakMaskPair> DiamondFlattenedEntryMasks;
    // Roadmap L375 (6th investigation): see `DiamondFlattener::MergeVisited`
    // for why this lives here -- one set per `Function`, outliving every
    // individual `DiamondFlattener`/`LoopLinearizer` instance constructed
    // below for it, so a block two of them separately claim as their own
    // diamond-start is caught instead of silently mutated twice.
    SmallPtrSet<BasicBlock *, 16> MergeVisited;
    // Roadmap L425: see `DiamondFlattener::ResolvedBlocks` for why this
    // lives here too, alongside `MergeVisited` with the same whole-
    // `Function` lifetime and sharing.
    SmallPtrSet<BasicBlock *, 16> ResolvedBlocks;
    {
      PostDominatorTree PDT(F);
      // Roadmap L447: `IsWholeFunctionPass=true` here, and only here --
      // see `DiamondFlattener::IsWholeFunctionPass`'s own comment.
      DiamondFlattener DF(F, DT, PDT, CI, UI, MergeVisited, ResolvedBlocks,
                          /*GetAlreadyLinearizedCycleExit=*/nullptr,
                          /*NarrowChildEntryMasks=*/nullptr,
                          /*IsWholeFunctionPass=*/true);
      Changed |= DF.run();
      // Roadmap L286: captured *before* `DF` (and the `PDT` it holds a
      // reference to) goes out of scope -- see `LoopLinearizer`'s own
      // `DiamondFlattenedEntryMasks` member comment for why this pass's
      // own record of "should this lane even enter this not-yet-
      // linearized cycle" must be threaded through explicitly rather than
      // left for `LoopLinearizer` to somehow rediscover on its own.
      DiamondFlattenedEntryMasks = DF.getCycleBoundaryMasks();
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
    PostDominatorTree PDT2(F);
    CycleInfo CI2;
    CI2.compute(F);
    UniformityInfo UI2 = computeWaveUniformity(F, DT2, CI2);
    bool CycleChanged =
        LoopLinearizer(F, DT2, PDT2, CI2, UI2, DiamondFlattenedEntryMasks,
                       MergeVisited, ResolvedBlocks)
            .run();
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
