//===- GroupShared.cpp - CPU target groupshared memory layout ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GroupShared.h"
#include "feme/Transforms/CPU/GroupSharedInfo.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ReplaceConstant.h"
#include "llvm/Support/Alignment.h"

using namespace llvm;

namespace feme::cpu {

namespace {

/// Returns the operand index of an `llvm.masked.gather`/`.scatter` call's
/// vector-of-pointers argument (0 for a gather, 1 for a scatter -- see
/// `IRBuilderBase::CreateMaskedGather`/`CreateMaskedScatter`), or
/// `std::nullopt` if \p CI calls neither. `feme::cpu::FunctionWidener`
/// (Phase 4) always lowers a divergent groupshared access to one of these
/// two intrinsics -- see `widenMaskedLoad`/`widenMaskedStore` (an already
/// governed-masked access) and `widenGroupSharedLoad`/`widenGroupSharedStore`
/// (a raw one, roadmap step R23) -- so by the time this canonicalization
/// runs (at the very end of `feme::cpu::SIMDizePass::widen`, after every
/// `feme.cpu.masked.*` call has already been widened away), a gather/
/// scatter call is the only surviving "masked access" shape left to
/// retarget, alongside a plain `load`/`store`/`atomicrmw`.
std::optional<unsigned> getGatherScatterPtrOperandNo(const CallInst *CI) {
  const auto *II = dyn_cast_or_null<IntrinsicInst>(CI);
  if (!II)
    return std::nullopt;
  if (II->getIntrinsicID() == Intrinsic::masked_gather)
    return 0;
  if (II->getIntrinsicID() == Intrinsic::masked_scatter)
    return 1;
  return std::nullopt;
}

/// Returns whether \p U is a groupshared access this pass knows how to
/// retarget directly: a plain `load`/`store`/`atomicrmw`, or a gather/
/// scatter call's pointer argument (see `getGatherScatterPtrOperandNo`).
bool isSupportedGroupSharedLeafUser(const User *U) {
  if (isa<LoadInst>(U) || isa<StoreInst>(U) || isa<AtomicRMWInst>(U))
    return true;
  const auto *CI = dyn_cast<CallInst>(U);
  return CI && getGatherScatterPtrOperandNo(CI).has_value();
}

/// If every uniform-address use of \p V (a groupshared global, or a
/// first-level `getelementptr` off one) is part of one or more same-value
/// `<W x ptr>` broadcasts a gather/scatter's pointer argument still needs
/// even when the underlying address never varies by lane (see
/// `feme::cpu::FunctionWidener::widenMaskedLoad`'s comment: "correct
/// whether that vector turns out to hold the same pointer in every lane
/// ... or a genuinely different one per lane"), returns the fully-built
/// `<W x ptr>` value of every such broadcast -- either shape `getWidened`
/// broadcasts a uniform value into:
///
///  - `IRBuilderBase::CreateVectorSplat`'s two-instruction
///    `insertelement`+`shufflevector` shape, when \p V is a
///    `getelementptr` (an `Instruction`, so `ConstantFolder` cannot fold
///    the broadcast away), or
///  - `llvm::convertUsersOfConstantsToInstructions`'s per-lane
///    `insertelement` chain, when \p V is a direct global reference (a
///    `Constant`, whose broadcast `ConstantFolder` folds straight back to
///    itself -- this materialized expansion, run just before this
///    function, is the only remaining trace of it).
///
/// \p V commonly feeds *more than one* independent broadcast within the
/// same function (roadmap L10): `convertUsersOfConstantsToInstructions`
/// memoizes its expansion of a broadcast-splat constant per (constant,
/// basic block) pair, so the same splat constant reused across multiple
/// basic blocks -- one per masked gather/scatter site `feme::cpu::
/// FunctionWidener` widens, however many that turns out to be -- expands
/// into one independent `insertelement` chain *per block* rather than a
/// single one shared function-wide; the `getelementptr`-based two-
/// instruction shape is likewise rebuilt fresh at every site, whenever
/// there is more than one. Every returned broadcast is guaranteed
/// well-formed (a full, `PoisonValue`-based, exactly-\p V-inserting chain
/// of precisely its own vector type's lane count).
///
/// Returns `std::nullopt` if any of \p V's `insertelement` users don't
/// belong to a well-formed broadcast of either shape above (`emitError`'s
/// "unrecognized broadcast" diagnostic point). Returns an empty list (not
/// `std::nullopt`) if \p V has no `insertelement` users at all (i.e. \p V
/// feeds only a leaf access directly, per `isSupportedGroupSharedLeafUser`,
/// with no broadcast at all).
std::optional<SmallVector<Value *, 4>> matchPointerBroadcasts(Value *V) {
  SmallVector<InsertElementInst *, 8> Links;
  for (User *U : V->users())
    if (auto *IE = dyn_cast<InsertElementInst>(U); IE && IE->getOperand(1) == V)
      Links.push_back(IE);
  if (Links.empty())
    return SmallVector<Value *, 4>{};

  SmallVector<Value *, 4> Results;
  unsigned LinksConsumed = 0;

  // A "final" link -- one nothing else in `Links` builds on top of -- is
  // the root of some independent broadcast chain feeding `V`; walk each
  // one back to see whether it forms a well-formed chain of either shape.
  for (InsertElementInst *IE : Links) {
    bool IsFinalLink = llvm::none_of(Links, [&](InsertElementInst *Other) {
      return Other != IE && Other->getOperand(0) == IE;
    });
    if (!IsFinalLink)
      continue;

    // The two-instruction `CreateVectorSplat` shape: this chain's only
    // link feeds a zero-splat `shufflevector` and nothing else.
    if (IE->hasOneUse()) {
      if (auto *SVI = dyn_cast<ShuffleVectorInst>(*IE->user_begin());
          SVI && SVI->getOperand(0) == IE && SVI->isZeroEltSplat()) {
        Results.push_back(SVI);
        ++LinksConsumed;
        continue;
      }
    }

    // The per-lane `insertelement`-chain shape: walk back through
    // operand(0) -- each earlier link must also insert `V` -- until a
    // `PoisonValue` base, verifying the walked length matches this
    // chain's own vector type's lane count exactly.
    auto *VecTy = dyn_cast<FixedVectorType>(IE->getType());
    if (!VecTy)
      return std::nullopt;
    unsigned ChainLen = 0;
    Value *Cur = IE;
    while (auto *CurIE = dyn_cast<InsertElementInst>(Cur)) {
      if (CurIE->getOperand(1) != V)
        return std::nullopt;
      ++ChainLen;
      Cur = CurIE->getOperand(0);
    }
    if (ChainLen != VecTy->getNumElements() || !isa<PoisonValue>(Cur))
      return std::nullopt;
    Results.push_back(IE);
    LinksConsumed += ChainLen;
  }

  if (LinksConsumed != Links.size())
    return std::nullopt; // A link belonged to no well-formed chain.
  return Results;
}

/// Whether every broadcast \p V feeds (per `matchPointerBroadcasts`) is
/// well-formed and feeds only a supported leaf access.
bool hasOnlySupportedBroadcasts(Value *V) {
  std::optional<SmallVector<Value *, 4>> Broadcasts = matchPointerBroadcasts(V);
  return Broadcasts && llvm::all_of(*Broadcasts, [](Value *Final) {
           return llvm::all_of(Final->users(), isSupportedGroupSharedLeafUser);
         });
}

/// `llvm::convertUsersOfConstantsToInstructions`'s per-`(Constant,
/// BasicBlock)` memoization (`llvm/lib/IR/ReplaceConstant.cpp`) can still
/// materialize more than one instance of the *same* constant-expression
/// `getelementptr` within a single basic block: its cache falls back to a
/// fresh expansion whenever the already-cached instance's position comes
/// after the point currently being expanded (see its own comment, "If the
/// cached instruction is after the insertion point, we need to create a
/// new one"), which processing a per-lane broadcast's own `insertelement`
/// chain -- several sibling instructions in program order, each
/// independently requiring the same constant materialized at its own
/// point, immediately before it -- reliably triggers once the worklist
/// that drives the expansion (a `SetVector`, popped LIFO) reaches those
/// siblings out of program order (roadmap L10). The result is several
/// structurally-identical (`Instruction::isIdenticalTo`), but distinct,
/// `GetElementPtrInst`s in the same block, each feeding exactly one link
/// of what is semantically a single broadcast chain --
/// `matchPointerBroadcasts` has no way to recognize that shape on its
/// own, since its per-lane walk assumes every link inserts the exact same
/// producer `Value`. Coalescing every group of structurally-identical
/// direct `getelementptr` users of \p GV within each basic block back
/// down to one canonical instance, immediately after
/// `convertUsersOfConstantsToInstructions` runs (and so before any
/// analysis below has to reason about the duplicates), restores that
/// assumption instead of teaching every later `matchPointerBroadcasts`
/// caller to compare producers by structural equivalence instead of
/// identity.
void coalesceIdenticalGroupSharedGEPs(GlobalVariable &GV, Function &F) {
  for (BasicBlock &BB : F) {
    SmallVector<GetElementPtrInst *, 8> GEPs;
    for (Instruction &I : BB)
      if (auto *GEP = dyn_cast<GetElementPtrInst>(&I);
          GEP && GEP->getPointerOperand() == &GV)
        GEPs.push_back(GEP);

    for (unsigned I = 0, E = GEPs.size(); I != E; ++I) {
      GetElementPtrInst *Canonical = GEPs[I];
      if (!Canonical)
        continue; // Already coalesced away as an earlier GEP's duplicate.
      for (unsigned J = I + 1; J != E; ++J) {
        GetElementPtrInst *Dup = GEPs[J];
        if (!Dup || !Canonical->isIdenticalTo(Dup))
          continue;
        Dup->replaceAllUsesWith(Canonical);
        Dup->eraseFromParent();
        GEPs[J] = nullptr;
      }
    }
  }
}

/// Rebuilds \p CI (a gather/scatter call whose `Ptrs` argument was \p
/// OldPtrs, an `addrspace(3)` vector) with \p NewPtrs (address space 0,
/// "the address space cast away") instead: unlike a plain `load`/`store`/
/// `atomicrmw`, whose pointer operand carries no address space of its
/// own in the instruction's type (an ordinary `Use::set` suffices, see the
/// call sites below), `llvm.masked.gather`/`.scatter` are overloaded
/// intrinsics mangled by their pointer vector's type, address space
/// included -- retargeting the argument in place would leave the call
/// referencing a declaration with the wrong mangled name for its new
/// operand type.
void rebuildGatherScatterCall(CallInst &CI, unsigned PtrOperandNo,
                              Value *NewPtrs, IRBuilder<> &Builder) {
  MaybeAlign Alignment = CI.getParamAlign(PtrOperandNo);
  if (PtrOperandNo == 0) {
    Value *Mask = CI.getArgOperand(1);
    Value *Passthru = CI.getArgOperand(2);
    Value *NewCall = Builder.CreateMaskedGather(CI.getType(), NewPtrs,
                                                Alignment.valueOrOne(), Mask,
                                                Passthru, CI.getName());
    CI.replaceAllUsesWith(NewCall);
  } else {
    Value *Data = CI.getArgOperand(0);
    Value *Mask = CI.getArgOperand(2);
    Builder.CreateMaskedScatter(Data, NewPtrs, Alignment.valueOrOne(), Mask);
  }
  CI.eraseFromParent();
}

/// Retargets every access this pass supports rooted at \p OldProducer (a
/// groupshared global, or a first-level `getelementptr` off one, already
/// verified valid by the validation pass below) onto \p NewProducer (the
/// corresponding flat, address-space-0 pointer, or `getelementptr` off
/// one). Handles both a direct leaf access and every uniform-address
/// broadcast `matchPointerBroadcasts` recognizes (there may be more than
/// one -- see its own comment).
void retargetGroupSharedProducer(Value *OldProducer, Value *NewProducer) {
  std::optional<SmallVector<Value *, 4>> OldWides =
      matchPointerBroadcasts(OldProducer);
  assert(OldWides && "already verified well-formed by "
                     "rewriteGroupSharedGlobals's own validation pass");
  for (Value *OldWide : *OldWides) {
    // A uniform address still reaching a gather/scatter (see
    // `matchPointerBroadcasts`'s comment): rebuild the broadcast around
    // `NewProducer` instead, right where the old one was built (so it
    // dominates every gather/scatter call the old one did), retarget
    // every gather/scatter it feeds, then erase the old, now-dead
    // broadcast chain.
    unsigned NumLanes =
        cast<FixedVectorType>(OldWide->getType())->getNumElements();
    IRBuilder<> SplatBuilder(cast<Instruction>(OldWide));
    Value *NewWide = SplatBuilder.CreateVectorSplat(
        NumLanes, NewProducer, NewProducer->getName() + ".splat");
    for (Use &U : make_early_inc_range(OldWide->uses())) {
      auto &CI = *cast<CallInst>(U.getUser());
      unsigned PtrOperandNo = *getGatherScatterPtrOperandNo(&CI);
      IRBuilder<> CallBuilder(&CI);
      rebuildGatherScatterCall(CI, PtrOperandNo, NewWide, CallBuilder);
    }

    // Erase in dependency order: `OldWide` is already unused (every use
    // was just retargeted above), so it is always safe to erase first;
    // each earlier link becomes unused in turn as the one built on top of
    // it is erased. Walking through `operand(0)` alone (rather than a
    // pre-collected set of every `insertelement` using `OldProducer`, as
    // a single-broadcast-per-function design could get away with) keeps
    // this scoped to *this* chain's own links -- `OldProducer` may feed
    // other, independent broadcast chains too (roadmap L10), each erased
    // by its own iteration of this loop instead.
    Instruction *Cur = cast<Instruction>(OldWide);
    while (Cur) {
      auto *Base = dyn_cast<InsertElementInst>(Cur->getOperand(0));
      Cur->eraseFromParent();
      Cur = (Base && Base->getOperand(1) == OldProducer) ? Base : nullptr;
    }
  }
  // `OldProducer` may still have other, non-broadcast uses (a direct
  // `load`/`store`/`atomicrmw` alongside every broadcast just retargeted
  // above) -- retarget those the ordinary way too. A `GetElementPtrInst`
  // use off `GV` itself is left untouched here: `GV` commonly feeds both
  // a direct broadcast (offset 0, needing no `getelementptr` at all) and
  // one or more first-level `getelementptr`s off it at other offsets in
  // the very same function -- each such GEP is retargeted by
  // `rewriteGroupSharedGlobals`'s own separate loop over `GEPs`, via its
  // own `retargetGroupSharedProducer(GEP, ...)` call, not this one. A
  // `GetElementPtrInst` use off a first-level GEP itself, though (roadmap
  // L11: the per-row-component address `widenGroupSharedLoad`'s vector
  // case builds, one per component of a divergent-address row load), has
  // no other loop to retarget it -- rebuild it off `NewProducer` with the
  // same indices and recurse into this same function for its own leaf
  // users instead of skipping it.
  for (Use &U : make_early_inc_range(OldProducer->uses())) {
    User *Usr = U.getUser();
    if (auto *NestedGEP = dyn_cast<GetElementPtrInst>(Usr)) {
      if (isa<GlobalVariable>(OldProducer))
        continue; // Handled by rewriteGroupSharedGlobals's own GEPs loop.
      IRBuilder<> NestedBuilder(NestedGEP);
      SmallVector<Value *, 4> Idxs(NestedGEP->indices());
      Value *NewNestedGEP = NestedBuilder.CreateGEP(
          NestedGEP->getSourceElementType(), NewProducer, Idxs,
          NestedGEP->getName(), NestedGEP->isInBounds());
      retargetGroupSharedProducer(NestedGEP, NewNestedGEP);
      NestedGEP->eraseFromParent();
      continue;
    }
    if (isa<LoadInst>(Usr) || isa<StoreInst>(Usr) || isa<AtomicRMWInst>(Usr)) {
      U.set(NewProducer);
      continue;
    }
    auto &CI = *cast<CallInst>(Usr);
    unsigned PtrOperandNo = *getGatherScatterPtrOperandNo(&CI);
    IRBuilder<> CallBuilder(&CI);
    rebuildGatherScatterCall(CI, PtrOperandNo, NewProducer, CallBuilder);
  }
}

} // namespace

GroupSharedLayout computeGroupSharedLayout(const Module &M) {
  GroupSharedLayout Layout;
  const DataLayout &DL = M.getDataLayout();
  uint64_t Offset = 0;
  Align Strictest(1);

  for (const GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() != GroupSharedAddressSpace)
      continue;
    Align GVAlign = DL.getPreferredAlign(&GV);
    Strictest = std::max(Strictest, GVAlign);
    Offset = alignTo(Offset, GVAlign);
    Layout.Offsets[&GV] = Offset;
    Offset += DL.getTypeAllocSize(GV.getValueType());
    if (GV.hasInitializer())
      Layout.NeedsZeroInit = true;
  }

  Layout.TotalSize = Offset;
  Layout.Alignment = Strictest.value();
  return Layout;
}

GroupSharedRequirements getGroupSharedRequirements(const Module &M) {
  GroupSharedLayout Layout = computeGroupSharedLayout(M);
  return GroupSharedRequirements{Layout.TotalSize, Layout.Alignment};
}

bool rewriteGroupSharedGlobals(Function &F, Value *GroupSharedBase,
                               const GroupSharedLayout &Layout) {
  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();
  Type *I8Ty = Type::getInt8Ty(Ctx);

  // Every use of a groupshared global, gathered up front so the pass can
  // bail without touching `F` at all if it finds a shape it does not
  // support: a scalar or vector-of-pointers (divergent index, roadmap step
  // R23) `getelementptr`/`load`/`store`/`atomicrmw`, or a gather/scatter
  // call (`feme::cpu::FunctionWidener`'s widening of a divergent access,
  // or of one reached through a `getelementptr` -- "access through a
  // getelementptr" and "a masked store at a uniform address", also R23),
  // each optionally reached through the uniform-address broadcast
  // `matchPointerBroadcasts` recognizes. An `atomicrmw` is accepted
  // alongside `load`/`store` (roadmap step R2, feme/docs/Roadmap.md's
  // §2.3 `histogram.hlsl`): its address is masked exactly the same way a
  // `load`/`store`'s is (see `feme::cpu::LinearizePass::maskMemoryOps`),
  // so it needs no different treatment here -- only the pointer operand's
  // address space changes, just as it does for `load`/`store`.
  for (auto &[GVConst, Offset] : Layout.Offsets) {
    auto *GV = const_cast<GlobalVariable *>(GVConst);
    if (GV->use_empty())
      continue;

    // Normalize any constant-expression use (e.g. a folded
    // `getelementptr` constant, or a broadcast splat of `GV` itself) within
    // `F` into ordinary instructions first, so every use handled below is a
    // plain instruction operand.
    convertUsersOfConstantsToInstructions({GV}, &F);
    coalesceIdenticalGroupSharedGEPs(*GV, F);

    for (Use &U : GV->uses()) {
      auto *UserInst = dyn_cast<Instruction>(U.getUser());
      if (UserInst && UserInst->getFunction() != &F)
        continue; // A different entry point's use of the same global.

      bool IsBroadcastLink =
          UserInst && isa<InsertElementInst>(UserInst) &&
          cast<InsertElementInst>(UserInst)->getOperand(1) == GV;
      if (!UserInst ||
          (!isSupportedGroupSharedLeafUser(UserInst) &&
           !isa<GetElementPtrInst>(UserInst) && !IsBroadcastLink)) {
        Ctx.emitError(
            "feme-cpu-simdize: groupshared global '" + GV->getName() +
            "' is used in a way this milestone does not yet canonicalize; "
            "only a getelementptr, load, store, atomicrmw, or masked "
            "gather/scatter is supported (roadmap milestone 9 deviation)");
        return false;
      }
      if (IsBroadcastLink) {
        if (!hasOnlySupportedBroadcasts(GV)) {
          Ctx.emitError(
              "feme-cpu-simdize: groupshared global '" + GV->getName() +
              "' feeds an unrecognized broadcast; only the uniform-address "
              "broadcast a masked gather/scatter's pointer argument needs "
              "is supported (roadmap milestone 9 deviation)");
          return false;
        }
        continue;
      }
      if (auto *GEP = dyn_cast<GetElementPtrInst>(UserInst)) {
        for (const User *GEPUser : GEP->users()) {
          if (isSupportedGroupSharedLeafUser(GEPUser))
            continue;
          auto *IE = dyn_cast<InsertElementInst>(GEPUser);
          if (IE && IE->getOperand(1) == GEP && hasOnlySupportedBroadcasts(GEP))
            continue;
          // (Roadmap L11) a second-level `getelementptr` off `GEP` --
          // the per-row-component address `FunctionWidener::
          // widenGroupSharedLoad`'s vector case builds off a divergent
          // row address, one per component of the loaded row -- is
          // supported too, as long as `GEP` itself is a *divergent*,
          // already-widened vector-of-pointers address (i.e. this is the
          // specific vector-row-component shape and not an ordinary
          // uniform nested array/struct access chain, which milestone
          // 9's own scope narrowing still leaves unsupported) and every
          // one of the nested GEP's own users is an ordinary leaf access
          // (in practice always a masked gather, one per component; a
          // divergent vector-typed groupshared load has no
          // uniform-address broadcast case to consider, unlike a scalar
          // one, since its address is already a real vector-of-pointers
          // `getelementptr` by construction).
          if (auto *NestedGEP = dyn_cast<GetElementPtrInst>(GEPUser);
              NestedGEP && GEP->getType()->isVectorTy() &&
              llvm::all_of(NestedGEP->users(), isSupportedGroupSharedLeafUser))
            continue;
          Ctx.emitError(
              "feme-cpu-simdize: groupshared global '" + GV->getName() +
              "' feeds a nested getelementptr or another unsupported "
              "user; only a first-level getelementptr feeding a direct "
              "load, store, atomicrmw, masked gather/scatter, or (for a "
              "vector-typed row load) a second-level per-component "
              "getelementptr feeding its own masked gather is supported "
              "(roadmap milestone 9 deviation)");
          return false;
        }
      }
    }
  }

  for (auto &[GVConst, Offset] : Layout.Offsets) {
    auto *GV = const_cast<GlobalVariable *>(GVConst);

    // Snapshot every top-level node `GV` feeds within `F` up front:
    // `retargetGroupSharedProducer` erases every use of a broadcast chain
    // (or a `getelementptr`) at once, which a use-list iterator cannot
    // survive being mutated underneath -- so this loop only ever performs
    // one retargeting operation per distinct node, never per individual
    // `Use`.
    SmallPtrSet<GetElementPtrInst *, 4> SeenGEPs;
    SmallVector<GetElementPtrInst *, 4> GEPs;
    SmallVector<Use *, 4> DirectLeafUses;
    bool HasBroadcast = false;
    for (Use &U : GV->uses()) {
      auto *UserInst = dyn_cast<Instruction>(U.getUser());
      if (!UserInst || UserInst->getFunction() != &F)
        continue;
      if (auto *GEP = dyn_cast<GetElementPtrInst>(UserInst)) {
        if (SeenGEPs.insert(GEP).second)
          GEPs.push_back(GEP);
      } else if (isa<InsertElementInst>(UserInst)) {
        HasBroadcast = true;
      } else {
        DirectLeafUses.push_back(&U);
      }
    }

    for (Use *U : DirectLeafUses) {
      IRBuilder<> Builder(cast<Instruction>(U->getUser()));
      Value *Flat =
          Builder.CreateGEP(I8Ty, GroupSharedBase, Builder.getInt64(Offset),
                            GV->getName() + ".flat");
      U->set(Flat);
    }

    if (HasBroadcast) {
      // Built at the function's own entry block rather than at any one
      // broadcast's own use site (as a single-broadcast-per-function
      // design could get away with): roadmap L10 found that `GV` commonly
      // feeds *more than one* independent broadcast (one per basic block,
      // per `matchPointerBroadcasts`'s own comment) -- a `Flat` computed
      // at just one of those sites would not dominate the others.
      IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());
      Value *Flat =
          Builder.CreateGEP(I8Ty, GroupSharedBase, Builder.getInt64(Offset),
                            GV->getName() + ".flat");
      retargetGroupSharedProducer(GV, Flat);
    }

    for (GetElementPtrInst *GEP : GEPs) {
      IRBuilder<> Builder(GEP);
      Value *Flat =
          Builder.CreateGEP(I8Ty, GroupSharedBase, Builder.getInt64(Offset),
                            GV->getName() + ".flat");
      SmallVector<Value *, 4> Idxs(GEP->indices());
      Value *NewGEP = Builder.CreateGEP(GEP->getSourceElementType(), Flat, Idxs,
                                        GEP->getName(), GEP->isInBounds());
      // `NewGEP` is address space 0 while `GEP` was `addrspace(3)` (the
      // address space "cast away", per the file comment above).
      retargetGroupSharedProducer(GEP, NewGEP);
      GEP->eraseFromParent();
    }
  }
  return true;
}

} // namespace feme::cpu
