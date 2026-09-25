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
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CycleAnalysis.h"
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
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

#include <optional>

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
void applyStageMasks(BasicBlock &BB, MaskPair &Masks,
                     SmallPtrSetImpl<Value *> *MaskedLoads = nullptr) {
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
static void
freezeLoopCarriedValues(BasicBlock *Header, BasicBlock *Latch, Value *BodyMask,
                        ArrayRef<PHINode *> MaskPhis,
                        function_ref<bool(const BasicBlock *)> InLoop) {
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
    bool UsedOutsideLoop = any_of(PN.users(), [&](User *U) {
      return !InLoop(cast<Instruction>(U)->getParent());
    });
    if (!UsedOutsideLoop)
      continue; // Only ever read inside the loop: nothing to freeze.
    PN.setIncomingValue(
        Idx, B.CreateSelect(BodyMask, NewValue, &PN, PN.getName() + ".frozen"));
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
  bool isLoopControlEdge(BasicBlock *Cur, BasicBlock *Target) {
    CycleRef C = CI.getCycle(Cur);
    if (Target == CI.getHeader(C))
      return true;
    return !CI.contains(C, Target);
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
  bool validate(BasicBlock *Start, BasicBlock *End, bool Quiet = false);

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
                                bool Quiet) {
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

    if (auto *UBr = dyn_cast<UncondBrInst>(Term)) {
      Cur = UBr->getSuccessor(0);
      continue;
    }

    auto *Br = dyn_cast<CondBrInst>(Term);
    if (!Br) {
      if (!Quiet)
        diagnose(F, "unsupported terminator '" +
                        Twine(Term->getOpcodeName()) +
                        "' in a linearizable region");
      return false;
    }

    BasicBlock *T = Br->getSuccessor(0);
    BasicBlock *Fsucc = Br->getSuccessor(1);

    if (isInCycle(Cur) &&
        (isLoopControlEdge(Cur, T) || isLoopControlEdge(Cur, Fsucc))) {
      CycleBoundaryBlocks.insert(Cur);
      // Roadmap L151: remember the reconvergence point (if any) this walk
      // was headed for when it stopped here, so `run` can seed this
      // cycle's own exit-block root with the *same* one (see `RootEnd`)
      // instead of always assuming the cycle's exit reaches a `ret`
      // directly. A cycle nested inside an enclosing diamond's arm (e.g.
      // a second, sibling loop inside an `if`'s true arm) has its exit
      // block's own downstream code -- up to and including that diamond's
      // merge -- *shared* with the enclosing diamond's other arm; without
      // this, the exit-block root's own `flatten` walk would independently
      // re-walk (and re-mask, with a stale, narrower mask) that same
      // shared tail after the enclosing root's walk already merged and
      // masked it correctly.
      CycleBoundaryEnd.try_emplace(Cur, End);
      return true; // Stop here; LoopLinearizer's problem, not an error.
    }

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

    if (!validate(T, R, Quiet) || !validate(Fsucc, R, Quiet))
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
  if (!validate(Start, R, /*Quiet=*/true) || !CycleBoundaryBlocks.empty())
    return std::nullopt;
  MaskPair AllActive{ConstantInt::getTrue(F.getContext()),
                     ConstantInt::getTrue(F.getContext())};
  flatten(Start, R, AllActive, /*RedirectTo=*/R);
  return R;
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
      return CycleBoundaryMasks.try_emplace(Cur, Masks).first->second;
    }

    applyStageMasks(*Cur, Masks, &MaskedLoadResults);

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
    if (!validate(Roots[I], RootEnd.lookup(Roots[I])))
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
  LoopLinearizer(Function &F, DominatorTree &DT, PostDominatorTree &PDT,
                CycleInfo &CI, UniformityInfo &UI)
      : F(F), DT(DT), PDT(PDT), CI(CI), UI(UI) {}

  /// Validates and linearizes every leaf cycle in \p F matching the shape
  /// this pass supports. Returns whether \p F was changed.
  bool run();

private:
  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
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
                      IsDivergentBranch) {
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
    std::optional<BasicBlock *> Relay = uniformRelayChain(
        Candidate, ExitBlock,
        [this](const CondBrInst *CBr) { return isDivergentBranch(CBr); });
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
  return isDivergentValue(Br->getCondition());
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
    // Roadmap L197: `DiamondFlattener::isLoopControlEdge` classifies loop-
    // control edges via a live `CI.contains` membership check (O(1), no
    // block-list walk) rather than any `CI.getExitBlocks`-derived list --
    // see its own comment for why both a live call and a precomputed
    // cache of that method were tried and rejected here (a genuine
    // use-after-free for the former, and staleness against blocks this
    // same fixed-point loop itself inserts for the latter). No extra
    // plumbing is needed at this call site as a result.
    DiamondFlattener DF(F, DT, PDT, CI, UI);
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

  std::optional<ExitCheck> HeaderExit = matchExitCheck(*Header, ExitBlock);
  std::optional<ExitCheck> LatchExit = matchExitCheck(*Latch, ExitBlock);
  bool HeaderDivergent = HeaderExit && isDivergentBranch(HeaderExit->Br);
  bool LatchDivergent = LatchExit && isDivergentBranch(LatchExit->Br);

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
    SmallVector<BasicBlock *, 2> DivergentCandidates;
    for (BasicBlock *BB : OtherCondBrBlocks)
      if (!PeeledFrom.contains(BB) &&
          isDivergentBranch(cast<CondBrInst>(BB->getTerminator())))
        DivergentCandidates.push_back(BB);

    // Roadmap L197: only bail out here if some `OtherCondBrBlocks` entry
    // is *itself* still genuinely divergent -- an already-linearized
    // nested child cycle contributes no `DivergentCandidates` entry at
    // all (see the comment above), so it never reaches this diagnostic;
    // it instead simply falls through, untouched, to the ordinary
    // `Header`/`Latch`-only handling below, exactly like any other
    // uniform pass-through block already does.
    if (!DivergentCandidates.empty() && (HeaderDivergent || LatchDivergent)) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has an internal branch in '" +
                      DivergentCandidates.front()->getName() +
                      "'; unsupported (roadmap milestone 6 deviation)");
      return false;
    }

    if (DivergentCandidates.size() > 1) {
      diagnose(F, "loop at '" + Header->getName() +
                      "' has more than one divergent exit check ('" +
                      DivergentCandidates[0]->getName() + "' and '" +
                      DivergentCandidates[1]->getName() +
                      "'); unsupported (roadmap milestone 6 deviation)");
      return false;
    }
    if (DivergentCandidates.empty()) {
      // Roadmap L197: nothing here is actually divergent -- either this
      // cycle has no divergent exit check at all (the pre-existing
      // "leave alone" case), or `Header`/`Latch` still has its own
      // divergent check while every `OtherCondBrBlocks` entry is already
      // uniform (e.g. an already-linearized nested child cycle's own
      // continuation check -- see the comment above). Either way,
      // nothing here needs its own special handling: fall through to the
      // ordinary `Header`/`Latch`-only linearization below, which itself
      // returns `false` (leave alone) in the genuinely-nothing-divergent
      // case via its own `!HeaderDivergent && !LatchDivergent` check.
    } else {
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
        collectUniformPassThroughRegion(Header, CheckBlock, ExitBlock, C,
                                        PeeledFrom);
    std::optional<SmallPtrSet<BasicBlock *, 8>> PostRegion =
        collectUniformPassThroughRegion(CheckExit->StayInLoop, Latch,
                                        ExitBlock, C, PeeledFrom);
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
    freezeLoopCarriedValues(
        Header, Latch, MasksAfterCheck.Live,
        {cast<PHINode>(Masks.Live), cast<PHINode>(Masks.SideEffect)},
        [&](const BasicBlock *BB) { return CI.contains(C, BB); });
    addLatchIncoming(Masks, MasksAfterCheck);
    return true;
    } // end DivergentCandidates-non-empty handling (Roadmap L197)
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
    Continue = createUniformMaskAny(B, MasksAfterLatchCheck.Live, "loop.continue");
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

  freezeLoopCarriedValues(
      Header, Latch, MasksAtLatch.Live,
      {cast<PHINode>(Masks.Live), cast<PHINode>(Masks.SideEffect)},
      [&](const BasicBlock *BB) { return CI.contains(C, BB); });
  addLatchIncoming(Masks, MasksAfterLatchCheck);
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
  // Roadmap L197: attempting `C` itself here (not just its children) is
  // *not yet enabled* -- confirmed, via a real Vulkan CTS shader
  // (`dEQP-VK.graphicsfuzz.cosh-return-inf-unused`, a genuinely 3-deep
  // nested-loop shape), to hang forever inside
  // `DiamondFlattener::flatten`'s own `for (;;)` walk when `C` is a
  // not-yet-leaf (parent) cycle: `flatten` keeps re-entering the same
  // block sequence without ever reaching its own `Cur == End`/`RedirectTo`
  // termination, root cause not yet found (a stale `isInCycle`/cycle-
  // boundary classification against `CI`'s own frozen cycle membership is
  // suspected -- see this row's own investigation notes -- but not yet
  // confirmed). The `getExitBlocks`/`DT`/`PDT` fixes above are genuine,
  // independently useful correctness improvements in their own right (the
  // second is a real latent hazard even for today's leaf-only-attempt
  // traversal, across *sibling* leaf cycles sharing one function -- see
  // the comment above), so they are kept and exercised even though the
  // actual traversal-order change they were built to enable is not yet
  // turned on. Only ever call `linearizeCycle` here for a genuine leaf
  // (`CI.children(C).empty()`) until that hang's root cause is found and
  // fixed by a future session.
  bool Changed = false;
  for (CycleRef Child : CI.children(C))
    Changed |= linearizeCyclePostOrder(Child);
  DT.recalculate(F);
  PDT.recalculate(F);
  if (CI.children(C).empty())
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
    PostDominatorTree PDT2(F);
    CycleInfo CI2;
    CI2.compute(F);
    UniformityInfo UI2 = computeWaveUniformity(F, DT2, CI2);
    bool CycleChanged = LoopLinearizer(F, DT2, PDT2, CI2, UI2).run();
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
