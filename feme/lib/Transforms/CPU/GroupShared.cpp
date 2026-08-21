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
/// first-level `getelementptr` off one) is part of the same-value `<W x
/// ptr>` broadcast a gather/scatter's pointer argument still needs even
/// when the underlying address never varies by lane (see
/// `feme::cpu::FunctionWidener::widenMaskedLoad`'s comment: "correct
/// whether that vector turns out to hold the same pointer in every lane
/// ... or a genuinely different one per lane"), returns the fully-built
/// `<W x ptr>` value -- either shape `getWidened` broadcasts a uniform
/// value into:
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
/// Returns `nullptr` if \p V's `insertelement` users don't form either
/// shape (i.e. \p V feeds a leaf access directly, per
/// `isSupportedGroupSharedLeafUser`, with no broadcast at all).
Value *matchPointerBroadcast(Value *V) {
  SmallVector<InsertElementInst *, 8> Links;
  for (User *U : V->users())
    if (auto *IE = dyn_cast<InsertElementInst>(U); IE && IE->getOperand(1) == V)
      Links.push_back(IE);
  if (Links.empty())
    return nullptr;

  if (Links.size() == 1 && Links[0]->hasOneUse()) {
    if (auto *SVI = dyn_cast<ShuffleVectorInst>(*Links[0]->user_begin());
        SVI && SVI->getOperand(0) == Links[0] && SVI->isZeroEltSplat())
      return SVI; // The two-instruction `CreateVectorSplat` shape.
  }

  // The per-lane `insertelement`-chain shape: exactly one link per vector
  // lane, threaded through each other's vector operand -- find the one
  // link nothing else in the chain builds on top of.
  auto *VecTy = dyn_cast<FixedVectorType>(Links[0]->getType());
  if (!VecTy || Links.size() != VecTy->getNumElements())
    return nullptr;
  InsertElementInst *Last = nullptr;
  for (InsertElementInst *IE : Links) {
    bool IsFinalLink = llvm::none_of(Links, [&](InsertElementInst *Other) {
      return Other != IE && Other->getOperand(0) == IE;
    });
    if (!IsFinalLink)
      continue;
    if (Last)
      return nullptr; // More than one -- not a single linear chain.
    Last = IE;
  }
  return Last;
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
/// one). Handles both a direct leaf access and one reached through the
/// uniform-address broadcast `matchPointerBroadcast` recognizes.
void retargetGroupSharedProducer(Value *OldProducer, Value *NewProducer) {
  if (Value *OldWide = matchPointerBroadcast(OldProducer)) {
    // A uniform address still reaching a gather/scatter (see
    // `matchPointerBroadcast`'s comment): rebuild the broadcast around
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

    SmallVector<Instruction *, 8> DeadChain;
    for (User *U : OldProducer->users())
      if (auto *IE = dyn_cast<InsertElementInst>(U);
          IE && IE->getOperand(1) == OldProducer)
        DeadChain.push_back(IE);
    if (auto *SVI = dyn_cast<ShuffleVectorInst>(OldWide))
      DeadChain.push_back(SVI);

    // Erase in dependency order: `OldWide` is already unused (every use
    // was just retargeted above), so it is always safe to erase first;
    // each earlier link becomes unused in turn as the one built on top of
    // it is erased.
    Instruction *Cur = cast<Instruction>(OldWide);
    while (Cur) {
      auto *Base = dyn_cast<Instruction>(Cur->getOperand(0));
      Cur->eraseFromParent();
      Cur = (Base && llvm::is_contained(DeadChain, Base)) ? Base : nullptr;
    }
    // `OldProducer` may still have other, non-broadcast uses (a direct
    // `load`/`store`/`atomicrmw` alongside the broadcast just retargeted
    // above) -- fall through to retarget those the ordinary way too.
  }

  for (Use &U : make_early_inc_range(OldProducer->uses())) {
    User *Usr = U.getUser();
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
  // `matchPointerBroadcast` recognizes. An `atomicrmw` is accepted
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
        if (!matchPointerBroadcast(GV) ||
            !llvm::all_of(matchPointerBroadcast(GV)->users(),
                          isSupportedGroupSharedLeafUser)) {
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
          if (IE && IE->getOperand(1) == GEP && matchPointerBroadcast(GEP) &&
              llvm::all_of(matchPointerBroadcast(GEP)->users(),
                           isSupportedGroupSharedLeafUser))
            continue;
          Ctx.emitError(
              "feme-cpu-simdize: groupshared global '" + GV->getName() +
              "' feeds a nested getelementptr or another unsupported "
              "user; only a first-level getelementptr feeding a direct "
              "load, store, atomicrmw, or masked gather/scatter is "
              "supported (roadmap milestone 9 deviation)");
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
      IRBuilder<> Builder(
          cast<Instruction>(*llvm::find_if(GV->users(), [&](User *U) {
            auto *IE = dyn_cast<InsertElementInst>(U);
            return IE && IE->getOperand(1) == GV;
          })));
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
