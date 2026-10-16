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
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Transforms/Utils/Local.h"

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
/// retarget directly: a plain `load`/`store`/`atomicrmw`/`cmpxchg`, a
/// `ptrtoint` (roadmap L429(a)-adjacent `OpPtrDiff`/`OpConvertPtrToU`
/// shape: the pointer's own address space never appears in a `ptrtoint`'s
/// result type, so retargeting its sole pointer operand needs no rebuild,
/// exactly like a `load`/`store`'s pointer operand below), or a
/// gather/scatter call's pointer argument (see
/// `getGatherScatterPtrOperandNo`).
bool isSupportedGroupSharedLeafUser(const User *U) {
  if (isa<LoadInst>(U) || isa<StoreInst>(U) || isa<AtomicRMWInst>(U) ||
      isa<AtomicCmpXchgInst>(U) || isa<PtrToIntInst>(U))
    return true;
  const auto *CI = dyn_cast<CallInst>(U);
  return CI && getGatherScatterPtrOperandNo(CI).has_value();
}

/// Returns whether every user of \p V (a vector-of-pointers value: a
/// genuinely divergent `getelementptr` (roadmap L11), or a uniform-
/// address broadcast's own final `insertelement`/`shufflevector` link
/// (roadmap L7o) `matchPointerBroadcasts` builds) is a leaf access this
/// pass knows how to retarget directly, or a second-level per-component
/// `getelementptr` into that vector-of-pointers row -- the address
/// `FunctionWidener::widenGroupSharedLoad`'s vector case builds one per
/// component of a vector-typed row, whether that row's own address is
/// naturally divergent (L11) or a broadcast of a uniform one (L7o:
/// `subgroupElect()`-style "exactly one invocation writes" code stores a
/// whole vector-typed row through a uniform, wave-shared address, so the
/// row address is broadcast to every lane's own masked-scatter slot
/// rather than genuinely varying per lane) -- whose own users are, in
/// turn, all direct leaf accesses.
bool isSupportedGroupSharedRowUser(const Value *V) {
  return llvm::all_of(V->users(), [V](const User *U) {
    if (isSupportedGroupSharedLeafUser(U))
      return true;
    const auto *NestedGEP = dyn_cast<GetElementPtrInst>(U);
    return NestedGEP && V->getType()->isVectorTy() &&
           llvm::all_of(NestedGEP->users(), isSupportedGroupSharedLeafUser);
  });
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
/// well-formed and feeds only a supported leaf access, or (roadmap L7o) a
/// second-level per-component `getelementptr` into the broadcast's own
/// vector-of-pointers result -- see `isSupportedGroupSharedRowUser`.
bool hasOnlySupportedBroadcasts(Value *V) {
  std::optional<SmallVector<Value *, 4>> Broadcasts = matchPointerBroadcasts(V);
  return Broadcasts &&
         llvm::all_of(*Broadcasts, isSupportedGroupSharedRowUser);
}

/// (Roadmap L430) Returns whether \p V is a `PHINode` or `SelectInst`
/// merging two or more groupshared-rooted pointers -- the canonical
/// `variablePointers` shape of a conditional or loop-carried iterator
/// selecting between addresses in the *same* groupshared global (a
/// self-referential loop backedge, e.g. `opptrdiff`'s own per-iteration
/// pointer advance) or, in the general case, two genuinely *different*
/// groupshared globals (e.g. `workgroup_two_buffers`'s own `cond ? bufA
/// : bufB` shape) -- that this pass additionally knows how to retarget,
/// alongside the plain leaf/broadcast/nested-GEP shapes above.
bool isGroupSharedMergeInst(const Value *V) {
  return isa<PHINode>(V) || isa<SelectInst>(V);
}

// (Roadmap L430) Forward-declared so `isSupportedGroupSharedNestedGEPUser`
// below can accept a nested `getelementptr` feeding a pointer-compare or a
// further pointer-merge (e.g. `opptrdiff`'s own self-referential
// loop-carried backedge, where the per-iteration advance GEP's *own* user
// is the very phi it was derived from) -- these are defined further below,
// alongside `isGroupSharedRootedPointer`, the backward-walk helper they both
// share.
bool isSupportedGroupSharedCompareUser(const ICmpInst *Cmp,
                                       const GroupSharedLayout &Layout);
bool isSupportedGroupSharedMergeUser(const Instruction *Merge,
                                     const GroupSharedLayout &Layout,
                                     SmallPtrSetImpl<const Instruction *>
                                         &VisitedMerges);

/// Returns whether every user of \p GEP (a `getelementptr` already
/// reached from a groupshared global, directly or through zero or more
/// earlier nested `getelementptr`s) is itself a supported leaf access
/// (roadmap L273/L274: a genuinely *uniform* multi-index-per-instruction
/// access chain into a struct/array field of a groupshared global --
/// e.g. `buf.data.field[k]`, glslang's own whole-`AtomicStruct`-field
/// copy pattern once per-element-decomposed, see
/// `CompositeExtract`/`CompositeInsert`'s own per-member expansion of a
/// whole-aggregate load/store -- routinely re-`getelementptr`s an
/// already-fully-constant-offset field GEP a further, arbitrary number of
/// times, one per array element, rather than reaching a leaf access in a
/// single additional `getelementptr` the way the divergent-row case
/// above does), a well-formed uniform-address broadcast of \p GEP itself
/// (`FunctionWidener::widenMaskedStore`'s ordinary, non-groupshared-
/// vector-typed scalar path always calls `getWidened` on its matched
/// store's own pointer operand regardless of how deeply nested it is,
/// broadcasting a uniform, deeply-nested field/array address exactly the
/// same way it would a first-level one -- see `hasOnlySupportedBroadcasts`),
/// another nested `getelementptr` recursively satisfying this same
/// condition, to any depth, a pointer-compare
/// (`isSupportedGroupSharedCompareUser`), or (roadmap L430) a pointer-merge
/// (`isSupportedGroupSharedMergeUser`) -- the shape `opptrdiff`/
/// `opptrequal`/`opptrnotequal`'s own loop-carried pointer iterator takes:
/// each loop iteration's own per-element advance GEP feeds *back* into the
/// very phi it was itself derived from, so that phi is one of *this* GEP's
/// own forward users, not just an ancestor reached by walking backward from
/// it. Unlike `isSupportedGroupSharedRowUser` above (gated to a
/// *divergent*, already-widened vector-of-pointers row address
/// specifically, per roadmap L11/L7o), this helper places no requirement
/// on \p GEP's own type at all -- it is exactly as applicable to a uniform
/// scalar-pointer nested chain as the caller's own single-level check
/// already was, just generalized to any depth instead of exactly one
/// further level. `retargetGroupSharedProducer` already rewrites a chain
/// like this correctly (it recurses through every nested `getelementptr`
/// level unconditionally); this validation pass had simply never been
/// widened to *accept* more than one further level, rejecting an otherwise
/// perfectly retargetable access outright.
bool isSupportedGroupSharedNestedGEPUser(const GetElementPtrInst *GEP,
                                        const GroupSharedLayout &Layout,
                                        SmallPtrSetImpl<const Instruction *>
                                            &VisitedMerges) {
  return llvm::all_of(GEP->users(), [GEP, &Layout, &VisitedMerges](const User *U) {
    if (isSupportedGroupSharedLeafUser(U))
      return true;
    if (auto *IE = dyn_cast<InsertElementInst>(U);
        IE && IE->getOperand(1) == GEP)
      return hasOnlySupportedBroadcasts(const_cast<GetElementPtrInst *>(GEP));
    if (const auto *Cmp = dyn_cast<ICmpInst>(U))
      return isSupportedGroupSharedCompareUser(Cmp, Layout);
    if (isGroupSharedMergeInst(U))
      return isSupportedGroupSharedMergeUser(cast<Instruction>(U), Layout,
                                             VisitedMerges);
    const auto *NestedGEP = dyn_cast<GetElementPtrInst>(U);
    return NestedGEP && isSupportedGroupSharedNestedGEPUser(NestedGEP, Layout,
                                                            VisitedMerges);
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
/// `getelementptr` users of \p GV -- directly, or through any depth of
/// earlier nested `getelementptr`s also rooted at \p GV (roadmap L274:
/// the same duplication `convertUsersOfConstantsToInstructions` can
/// produce for a first-level GEP applies identically to a *nested* one, a
/// deeply-nested field/array access's own per-lane broadcast chain
/// materializing its own several structurally-identical, but distinct,
/// instances the exact same way) -- within each basic block back down to
/// one canonical instance apiece, immediately after
/// `convertUsersOfConstantsToInstructions` runs (and so before any
/// analysis below has to reason about the duplicates), restores that
/// assumption instead of teaching every later `matchPointerBroadcasts`
/// caller to compare producers by structural equivalence instead of
/// identity. Runs to a fixpoint (coalescing one nesting level can only
/// make a *deeper* level's own GEPs identical, by updating their now-
/// shared pointer operand, never the reverse), so this remains correct
/// regardless of the two levels' relative program order.
bool isGroupSharedRootedGEP(const GetElementPtrInst *GEP,
                           const GlobalVariable &GV) {
  const Value *Ptr = GEP->getPointerOperand();
  while (const auto *ParentGEP = dyn_cast<GetElementPtrInst>(Ptr))
    Ptr = ParentGEP->getPointerOperand();
  return Ptr == &GV;
}

void coalesceIdenticalGroupSharedGEPs(GlobalVariable &GV, Function &F) {
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (BasicBlock &BB : F) {
      SmallVector<GetElementPtrInst *, 8> GEPs;
      for (Instruction &I : BB)
        if (auto *GEP = dyn_cast<GetElementPtrInst>(&I);
            GEP && isGroupSharedRootedGEP(GEP, GV))
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
          Changed = true;
        }
      }
    }
  }
}

/// (Roadmap L430) Returns whether \p V -- an operand of some
/// `isGroupSharedMergeInst` merge, or one of an `icmp`'s two pointer
/// operands -- is itself something this pass can always resolve to a
/// flat, address-space-0 pointer: a groupshared global (recognized via \p
/// Layout, the same layout `rewriteGroupSharedGlobals`'s own caller
/// already computed), a `getelementptr` chain rooted at one (to any
/// depth), a further merge recursively satisfying this same condition
/// (\p Visited guards a `PHINode`'s own loop-carried backedge -- see
/// `isPhiSelfReferentialBackedge`'s analogous comment in
/// `SPIRVResourceLowering.cpp` for why a cycle must be *tolerated*, not
/// rejected, here: the cycle is resolved by `retargetGroupSharedMergesAndCompares`'s
/// own pre-registration of a merge's flattened replacement before
/// recursing into its incoming values, not by this validation pass), or
/// a `variablePointers` null-check's own `ConstantPointerNull`/`poison`/
/// `undef` placeholder (a structurally-dead edge no handle chain will
/// ever resolve, matching `SPIRVResourceLowering.cpp`'s own identical
/// treatment of the same shape for resource-handle pointers).
bool isGroupSharedRootedPointer(const Value *V, const GroupSharedLayout &Layout,
                                SmallPtrSetImpl<const Value *> &Visited) {
  // (Roadmap L430) `ConstantAggregateZero` is a divergent (widened)
  // vector-of-null-pointers' own sentinel form -- `ConstantPointerNull`
  // only ever covers the scalar case -- see `flattenGroupSharedValue`'s
  // identical pairing.
  if (isa<ConstantPointerNull>(V) || isa<ConstantAggregateZero>(V) ||
      isa<UndefValue>(V) || isa<PoisonValue>(V))
    return true;
  if (const auto *GV = dyn_cast<GlobalVariable>(V))
    return Layout.Offsets.count(GV) != 0;
  if (const auto *GEP = dyn_cast<GetElementPtrInst>(V))
    return isGroupSharedRootedPointer(GEP->getPointerOperand(), Layout,
                                      Visited);
  if (isGroupSharedMergeInst(V)) {
    if (!Visited.insert(V).second)
      return true; // A cycle (self-referential backedge); trust it --
                   // resolved by `retargetGroupSharedMergesAndCompares`'s
                   // own pre-registration, not by this validation walk.
    if (const auto *Phi = dyn_cast<PHINode>(V))
      return llvm::all_of(Phi->incoming_values(), [&](const Use &U) {
        return isGroupSharedRootedPointer(U.get(), Layout, Visited);
      });
    const auto *Sel = cast<SelectInst>(V);
    return isGroupSharedRootedPointer(Sel->getTrueValue(), Layout, Visited) &&
           isGroupSharedRootedPointer(Sel->getFalseValue(), Layout, Visited);
  }
  return false;
}

/// (Roadmap L430) Returns whether \p Cmp -- an `icmp eq`/`icmp ne`
/// comparing two groupshared pointers (`OpPtrEqual`/`OpPtrNotEqual`'s own
/// lowering) -- has both operands resolvable the same way
/// `isGroupSharedRootedPointer` resolves a merge's own operands. Unlike a
/// merge, `Cmp` itself produces an `i1`, not a pointer, so there is no
/// further forward chain to validate past it.
bool isSupportedGroupSharedCompareUser(const ICmpInst *Cmp,
                                       const GroupSharedLayout &Layout) {
  SmallPtrSet<const Value *, 8> Visited;
  return isGroupSharedRootedPointer(Cmp->getOperand(0), Layout, Visited) &&
         isGroupSharedRootedPointer(Cmp->getOperand(1), Layout, Visited);
}

/// (Roadmap L430) Returns whether \p Merge (already known to be an
/// `isGroupSharedMergeInst`) is a groupshared pointer merge this pass
/// fully supports: every incoming/operand slot is
/// `isGroupSharedRootedPointer`, and every one of `Merge`'s own users is
/// itself a supported leaf access, broadcast link, nested `getelementptr`
/// chain, pointer-compare (`isSupportedGroupSharedCompareUser`), or
/// another merge recursively satisfying this same condition (\p
/// VisitedMerges guards both a direct self-loop and a longer
/// mutually-recursive merge chain from being validated more than once).
bool isSupportedGroupSharedMergeUser(const Instruction *Merge,
                                     const GroupSharedLayout &Layout,
                                     SmallPtrSetImpl<const Instruction *>
                                         &VisitedMerges) {
  if (!VisitedMerges.insert(Merge).second)
    return true;

  SmallPtrSet<const Value *, 8> RootVisited;
  if (const auto *Phi = dyn_cast<PHINode>(Merge)) {
    if (!llvm::all_of(Phi->incoming_values(), [&](const Use &U) {
          return isGroupSharedRootedPointer(U.get(), Layout, RootVisited);
        }))
      return false;
  } else {
    const auto *Sel = cast<SelectInst>(Merge);
    if (!isGroupSharedRootedPointer(Sel->getTrueValue(), Layout, RootVisited) ||
        !isGroupSharedRootedPointer(Sel->getFalseValue(), Layout, RootVisited))
      return false;
  }

  return llvm::all_of(Merge->users(), [&](const User *U) -> bool {
    if (isSupportedGroupSharedLeafUser(U))
      return true;
    if (const auto *Cmp = dyn_cast<ICmpInst>(U))
      return isSupportedGroupSharedCompareUser(Cmp, Layout);
    if (const auto *IE = dyn_cast<InsertElementInst>(U);
        IE && IE->getOperand(1) == Merge)
      return hasOnlySupportedBroadcasts(const_cast<Instruction *>(Merge));
    if (const auto *NestedGEP = dyn_cast<GetElementPtrInst>(U))
      return isSupportedGroupSharedNestedGEPUser(NestedGEP, Layout,
                                                 VisitedMerges);
    if (const auto *EE = dyn_cast<ExtractElementInst>(U))
      return Merge->getType()->isVectorTy() &&
             llvm::all_of(EE->users(), isSupportedGroupSharedLeafUser);
    if (isGroupSharedMergeInst(U))
      return isSupportedGroupSharedMergeUser(cast<Instruction>(U), Layout,
                                             VisitedMerges);
    return false;
  });
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
    // `OldWide`'s own uses are either a direct gather/scatter call (the
    // pre-existing, always-uniform-address shape), or (roadmap L7o) a
    // second-level per-component `getelementptr` into this broadcast row
    // -- recursing into this same function handles both: `OldWide` has no
    // `insertelement` users of its own (nothing re-broadcasts an already
    // broadcast vector), so `matchPointerBroadcasts(OldWide)` is always
    // empty and this immediately falls through to the ordinary direct-use
    // retargeting below, exactly like retargeting a first-level `GEP`'s
    // own leaf/nested-GEP uses does.
    retargetGroupSharedProducer(OldWide, NewWide);

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
      // (Roadmap L430) A self-referential loop-carried backedge:
      // `NestedGEP` is simultaneously a *user* of `OldProducer` (its own
      // pointer operand) and one of `OldProducer`'s *own* incoming
      // operands, when `OldProducer` is itself the merge whose own
      // flattening drove this call -- e.g. `opptrdiff`'s own
      // per-iteration pointer-advance GEP feeding back into the very phi
      // it was derived from. Eagerly retargeting-and-erasing it here, the
      // way an ordinary forward-only nested GEP chain is, would erase it
      // while `OldProducer` (not erased until every leg of every
      // top-level merge is retargeted -- see
      // `retargetGroupSharedMergesAndCompares`'s own erase loop) still
      // references it as a live incoming value. Leave it exactly where
      // the `isGroupSharedMergeInst(Usr)` case below leaves a genuine
      // merge-to-merge edge instead: `flattenGroupSharedValue` already
      // built its flat replacement when resolving `OldProducer`'s own
      // incoming slot, and `retargetGroupSharedMergesAndCompares`'s own
      // `RecursivelyDeleteTriviallyDeadInstructions` sweep erases it once
      // `OldProducer` itself is gone.
      bool IsOwnOperand = false;
      if (auto *Phi = dyn_cast<PHINode>(OldProducer))
        IsOwnOperand = llvm::is_contained(Phi->incoming_values(), NestedGEP);
      else if (auto *Sel = dyn_cast<SelectInst>(OldProducer))
        IsOwnOperand = Sel->getTrueValue() == NestedGEP ||
                       Sel->getFalseValue() == NestedGEP;
      if (IsOwnOperand)
        continue;
      IRBuilder<> NestedBuilder(NestedGEP);
      SmallVector<Value *, 4> Idxs(NestedGEP->indices());
      Value *NewNestedGEP = NestedBuilder.CreateGEP(
          NestedGEP->getSourceElementType(), NewProducer, Idxs,
          NestedGEP->getName(), NestedGEP->isInBounds());
      retargetGroupSharedProducer(NestedGEP, NewNestedGEP);
      NestedGEP->eraseFromParent();
      continue;
    }
    if (auto *EE = dyn_cast<ExtractElementInst>(Usr)) {
      // A per-lane scalar address `widenGroupSharedAtomicRMW`/
      // `widenGroupSharedAtomicCmpXchg` extract out of a genuinely
      // divergent vector-of-pointers address (see
      // `rewriteGroupSharedGlobals`'s own validation comment above) --
      // like `NestedGEP` just above, `EE`'s own cached result type
      // depends on its vector operand's element type (here, the pointer
      // address space `NewProducer`'s own flat/addrspace(0) rebuild
      // changes from `OldProducer`'s addrspace(3)), so it must be rebuilt
      // rather than mutated with a plain `U.set()` the way a `load`/
      // `store`/`atomicrmw`/`cmpxchg`'s own pointer operand below can be.
      IRBuilder<> EEBuilder(EE);
      Value *NewEE = EEBuilder.CreateExtractElement(
          NewProducer, EE->getIndexOperand(), EE->getName());
      retargetGroupSharedProducer(EE, NewEE);
      EE->eraseFromParent();
      continue;
    }
    if (isa<LoadInst>(Usr) || isa<StoreInst>(Usr) || isa<AtomicRMWInst>(Usr) ||
        isa<AtomicCmpXchgInst>(Usr) || isa<PtrToIntInst>(Usr)) {
      U.set(NewProducer);
      continue;
    }
    if (isGroupSharedMergeInst(Usr)) {
      // Roadmap L430: `Usr` (a `PHINode`/`SelectInst` merging two or more
      // groupshared-rooted pointers, possibly rooted at *different*
      // globals -- the canonical `variablePointers` cross-pointer-merge
      // shape) is retargeted wholesale by its own, separate top-level
      // call into `retargetGroupSharedMergesAndCompares`'s own
      // `Flatten`/erase sequence, not by this per-use dispatch: every one
      // of its own incoming/operand slots is resolved independently
      // there (see that function's comment), so `OldProducer`'s own
      // stale edge into it is simply left in place here and cleaned up
      // once `Usr` itself is replaced and erased.
      continue;
    }
    auto &CI = *cast<CallInst>(Usr);
    unsigned PtrOperandNo = *getGatherScatterPtrOperandNo(&CI);
    IRBuilder<> CallBuilder(&CI);
    rebuildGatherScatterCall(CI, PtrOperandNo, NewProducer, CallBuilder);
  }
}

/// (Roadmap L430) Resolves \p V -- already validated as
/// `isGroupSharedRootedPointer` -- to its own flat, address-space-0
/// equivalent, building whatever new instructions are needed (memoized
/// in \p FlatCache, shared across every call so e.g. two different
/// merges rooted at the same global's same `getelementptr` reuse one
/// flat rebuild, and so a merge referenced from more than one other
/// merge's own operand -- a merge-of-merges chain -- is itself flattened
/// only once):
///
///  - A `ConstantPointerNull`/`UndefValue`/`PoisonValue` resolves to the
///    identically-kinded flat-pointer-typed constant.
///  - A groupshared global resolves to a `getelementptr` off \p
///    GroupSharedBase at its own `Layout`-assigned byte offset, built at
///    \p F's entry block (so it dominates every one of the global's own
///    uses, mirroring `rewriteGroupSharedGlobals`'s own `HasBroadcast`
///    case for exactly the same reason: a global commonly feeds more
///    than one independent merge).
///  - A `getelementptr` resolves to a new `getelementptr` with the same
///    indices, built at the old one's own position (which already
///    dominates every one of its uses, including a merge consuming it),
///    off its own pointer operand's own flattened resolution.
///  - A `PHINode`/`SelectInst` (`isGroupSharedMergeInst`) resolves to a
///    brand new phi/select of the same shape. The replacement phi is
///    registered in \p FlatCache *before* its own incoming values are
///    recursively resolved (mirroring `SPIRVResourceLowering.cpp`'s own
///    `PointerMergeState` cycle-safety design, simplified here since this
///    pass already knows every global's final flat offset up front and so
///    never needs to *wait* for a value to arrive from a separate,
///    later walk the way that resource-handle design does): a
///    self-referential loop-carried backedge (`opptrdiff`/`opptrequal`'s
///    own per-iteration pointer-advance shape, where one incoming value's
///    own `getelementptr` chain walks back to the very phi being
///    resolved) re-enters this function for that same phi mid-recursion,
///    finds it already cached, and returns the new phi immediately
///    instead of looping forever.
Value *flattenGroupSharedValue(Value *V, Function &F, Value *GroupSharedBase,
                               Type *I8Ty, const GroupSharedLayout &Layout,
                               DenseMap<Value *, Value *> &FlatCache) {
  if (Value *Cached = FlatCache.lookup(V))
    return Cached;

  // (Roadmap L430) `V` itself may be a *divergent* merge/sentinel --
  // post-`FunctionWidener` widening left it `<WaveSize x ptr
  // addrspace(3)>` rather than a plain scalar pointer (e.g. the
  // loop-carried iterator `phi` in `opptrdiff.variable_pointers_wg_diff`
  // is divergent once different lanes take different trip counts) -- so
  // the flat replacement must match that same vector shape, not always
  // `GroupSharedBase`'s own scalar type. A nested `getelementptr` needs
  // no such widening here: `CreateGEP` below already widens its result
  // to match whichever of its own operands (most commonly a vector
  // index) is itself a vector, exactly mirroring the original GEP.
  Type *FlatPtrTy = GroupSharedBase->getType();
  if (auto *VecTy = dyn_cast<VectorType>(V->getType()))
    FlatPtrTy = VectorType::get(FlatPtrTy, VecTy->getElementCount());
  if (isa<ConstantPointerNull>(V) || isa<ConstantAggregateZero>(V))
    return FlatCache[V] = Constant::getNullValue(FlatPtrTy);
  if (isa<PoisonValue>(V))
    return FlatCache[V] = PoisonValue::get(FlatPtrTy);
  if (isa<UndefValue>(V))
    return FlatCache[V] = UndefValue::get(FlatPtrTy);

  if (auto *GV = dyn_cast<GlobalVariable>(V)) {
    IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());
    Value *Flat = Builder.CreateGEP(I8Ty, GroupSharedBase,
                                    Builder.getInt64(Layout.Offsets.lookup(GV)),
                                    GV->getName() + ".flat");
    return FlatCache[V] = Flat;
  }

  if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
    Value *FlatBase = flattenGroupSharedValue(
        GEP->getPointerOperand(), F, GroupSharedBase, I8Ty, Layout, FlatCache);
    IRBuilder<> Builder(GEP);
    SmallVector<Value *, 4> Idxs(GEP->indices());
    Value *NewGEP =
        Builder.CreateGEP(GEP->getSourceElementType(), FlatBase, Idxs,
                          GEP->getName(), GEP->isInBounds());
    return FlatCache[V] = NewGEP;
  }

  if (auto *Phi = dyn_cast<PHINode>(V)) {
    IRBuilder<> Builder(Phi);
    PHINode *NewPhi = Builder.CreatePHI(FlatPtrTy, Phi->getNumIncomingValues(),
                                        Phi->getName());
    FlatCache[V] = NewPhi; // Registered before recursing: see this
                          // function's own comment on self-referential
                          // backedges.
    for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I)
      NewPhi->addIncoming(
          flattenGroupSharedValue(Phi->getIncomingValue(I), F, GroupSharedBase,
                                  I8Ty, Layout, FlatCache),
          Phi->getIncomingBlock(I));
    return NewPhi;
  }

  auto *Sel = cast<SelectInst>(V);
  Value *NewTrue = flattenGroupSharedValue(Sel->getTrueValue(), F,
                                           GroupSharedBase, I8Ty, Layout,
                                           FlatCache);
  Value *NewFalse = flattenGroupSharedValue(Sel->getFalseValue(), F,
                                            GroupSharedBase, I8Ty, Layout,
                                            FlatCache);
  IRBuilder<> Builder(Sel);
  Value *NewSel = Builder.CreateSelect(Sel->getCondition(), NewTrue, NewFalse,
                                       Sel->getName());
  return FlatCache[V] = NewSel;
}

/// (Roadmap L430) Retargets every groupshared pointer-merge
/// (`isGroupSharedMergeInst`) and pointer-compare (`icmp eq`/`icmp ne` on
/// two groupshared pointers) in \p F onto flat, address-space-0
/// equivalents, before `rewriteGroupSharedGlobals`'s own per-global
/// `GEPs` rewrite loop runs: unlike that loop (which retargets one
/// global's own GEPs/leaf-uses/broadcasts at a time, in whatever order
/// \p Layout.Offsets happens to iterate), a merge may combine legs
/// rooted at two *different* globals processed in two different
/// iterations of that loop -- `flattenGroupSharedValue` resolves every
/// leg eagerly instead, using \p Layout's already-fully-known offsets
/// directly, so no per-global iteration order or multi-pass "wait for
/// the other leg to arrive" bookkeeping (`SPIRVResourceLowering.cpp`'s
/// own `PointerMergeMap`, built for resource handles with no such
/// up-front layout available) is needed here.
///
/// Every `icmp`'s pointer operands are retargeted in place (an `icmp`
/// needs no rebuild: its own result type never depends on its operands'
/// address space, see `isSupportedGroupSharedLeafUser`'s `ptrtoint`
/// comment for the identical reasoning). Every merge is instead replaced
/// wholesale: `retargetGroupSharedProducer` redirects its own non-merge
/// users (merge-to-merge edges are left to resolve themselves, as that
/// function's own comment on `isGroupSharedMergeInst` describes), every
/// old merge's remaining merge-only uses are then cleared with `poison`
/// (safe, since the merge instructions still holding them are themselves
/// about to be erased), and finally every old merge -- and any
/// now-wholly-unused helper `getelementptr` `flattenGroupSharedValue`
/// built only to feed one of its incoming slots (e.g. a loop-carried
/// backedge GEP whose sole remaining use was the just-erased old phi) --
/// is deleted.
void retargetGroupSharedMergesAndCompares(Function &F, Value *GroupSharedBase,
                                          const GroupSharedLayout &Layout) {
  LLVMContext &Ctx = F.getContext();
  Type *I8Ty = Type::getInt8Ty(Ctx);

  SmallVector<Instruction *, 8> Merges;
  SmallVector<ICmpInst *, 8> Compares;
  SmallPtrSet<Instruction *, 8> Seen;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      // (Roadmap L430) A merge/compare over a *divergent* groupshared
      // pointer is left `<WaveSize x ptr addrspace(3)>` by
      // `FunctionWidener`'s own widening, not a plain scalar pointer
      // (see `flattenGroupSharedValue`'s identical vector-aware
      // handling) -- `getScalarType()` strips any vector wrapper first
      // so both shapes are recognized here.
      auto IsGroupSharedPtr = [](Type *Ty) {
        auto *PtrTy = dyn_cast<PointerType>(Ty->getScalarType());
        return PtrTy && PtrTy->getAddressSpace() == GroupSharedAddressSpace;
      };
      if (isGroupSharedMergeInst(&I) && IsGroupSharedPtr(I.getType()) &&
          Seen.insert(&I).second)
        Merges.push_back(&I);
      else if (auto *Cmp = dyn_cast<ICmpInst>(&I);
              Cmp && IsGroupSharedPtr(Cmp->getOperand(0)->getType()))
        Compares.push_back(Cmp);
    }
  }
  if (Merges.empty() && Compares.empty())
    return;

  DenseMap<Value *, Value *> FlatCache;
  for (ICmpInst *Cmp : Compares) {
    Cmp->setOperand(0, flattenGroupSharedValue(Cmp->getOperand(0), F,
                                               GroupSharedBase, I8Ty, Layout,
                                               FlatCache));
    Cmp->setOperand(1, flattenGroupSharedValue(Cmp->getOperand(1), F,
                                               GroupSharedBase, I8Ty, Layout,
                                               FlatCache));
  }

  for (Instruction *Merge : Merges) {
    Value *NewMerge =
        flattenGroupSharedValue(Merge, F, GroupSharedBase, I8Ty, Layout,
                                FlatCache);
    retargetGroupSharedProducer(Merge, NewMerge);
  }
  // Every merge's own forward uses are now fully retargeted above; clear
  // any remaining stale merge-to-merge edge (one old merge still used as
  // another, not-yet-erased old merge's own operand) before erasing, then
  // erase every old merge -- order among themselves does not matter, since
  // none still reference each other as a live use.
  for (Instruction *Merge : Merges)
    Merge->replaceAllUsesWith(PoisonValue::get(Merge->getType()));
  for (Instruction *Merge : Merges)
    Merge->eraseFromParent();

  // Clean up any now-wholly-unused helper `getelementptr` built only to
  // feed a merge's own incoming slot (see this function's own comment).
  SmallVector<WeakTrackingVH, 8> DeadCandidates;
  for (auto &[OldVal, NewVal] : FlatCache)
    if (auto *OldInst = dyn_cast<Instruction>(OldVal); OldInst && !Seen.count(OldInst))
      DeadCandidates.push_back(OldInst);
  for (WeakTrackingVH &VH : DeadCandidates)
    if (Value *V = VH)
      RecursivelyDeleteTriviallyDeadInstructions(V);
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
      // (Roadmap L430) `GV` may also feed a pointer-compare directly
      // (`isSupportedGroupSharedCompareUser`), or a `PHINode`/`SelectInst`
      // merge directly (`isGroupSharedRootedGEP`'s own GEP-chain walk
      // treats a merge the same as any other rooted pointer, so `GV` used
      // as one of its own legs with no intervening `getelementptr` -- a
      // zero-offset access -- is exactly as valid a leg as a GEP off
      // `GV`): see `opptrdiff`/`opptrequal`'s own self-referential
      // loop-carried iterator shape, where `GV` itself is one of a phi's
      // two incoming values.
      bool IsSupportedCompare =
          UserInst && isa<ICmpInst>(UserInst) &&
          isSupportedGroupSharedCompareUser(cast<ICmpInst>(UserInst), Layout);
      bool IsSupportedMerge = UserInst && isGroupSharedMergeInst(UserInst);
      if (IsSupportedMerge) {
        SmallPtrSet<const Instruction *, 8> VisitedMerges;
        IsSupportedMerge = isSupportedGroupSharedMergeUser(
            cast<Instruction>(UserInst), Layout, VisitedMerges);
      }
      if (!UserInst ||
          (!isSupportedGroupSharedLeafUser(UserInst) &&
           !isa<GetElementPtrInst>(UserInst) && !IsBroadcastLink &&
           !IsSupportedCompare && !IsSupportedMerge)) {
        Ctx.emitError(
            "feme-cpu-simdize: groupshared global '" + GV->getName() +
            "' is used in a way this milestone does not yet canonicalize; "
            "only a getelementptr, load, store, atomicrmw, masked "
            "gather/scatter, pointer compare, or pointer merge (select/phi) "
            "is supported (roadmap milestone 9 deviation)");
        return false;
      }
      if (IsSupportedCompare || IsSupportedMerge)
        continue;
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
          if (const auto *Cmp = dyn_cast<ICmpInst>(GEPUser);
              Cmp && isSupportedGroupSharedCompareUser(Cmp, Layout))
            continue;
          if (isGroupSharedMergeInst(GEPUser)) {
            SmallPtrSet<const Instruction *, 8> VisitedMerges;
            if (isSupportedGroupSharedMergeUser(cast<Instruction>(GEPUser),
                                                Layout, VisitedMerges))
              continue;
          }
          auto *IE = dyn_cast<InsertElementInst>(GEPUser);
          if (IE && IE->getOperand(1) == GEP && hasOnlySupportedBroadcasts(GEP))
            continue;
          // A nested `getelementptr` off `GEP`, to any further depth
          // (roadmap L273/L274): covers both the pre-existing
          // divergent-row-component shape (`FunctionWidener::
          // widenGroupSharedLoad`'s vector case builds one per component
          // of a divergent row address, exactly one further GEP level
          // deep) and a genuinely *uniform* nested struct/array access
          // chain reaching a supported leaf several `getelementptr`
          // levels deep instead of just one -- e.g. glslang's own
          // whole-`AtomicStruct`-field copy pattern, once decomposed
          // per-array-element, routinely re-`getelementptr`s an
          // already-fully-constant-offset field GEP a further, arbitrary
          // number of times, one per element, rather than reaching its
          // leaf load/store in a single additional level. A *uniform* row
          // address broadcast into a vector-of-pointers (roadmap L7o:
          // `subgroupElect()`-style "exactly one invocation writes" code
          // stores a whole vector-typed row through a single, wave-shared
          // address rather than a genuinely divergent one) reaching a
          // second-level GEP is handled above instead, by
          // `hasOnlySupportedBroadcasts`'s own `isSupportedGroupSharedRowUser`
          // check on the broadcast link.
          if (auto *NestedGEP = dyn_cast<GetElementPtrInst>(GEPUser)) {
            SmallPtrSet<const Instruction *, 8> VisitedMerges;
            if (isSupportedGroupSharedNestedGEPUser(NestedGEP, Layout,
                                                    VisitedMerges))
              continue;
          }
          // A per-lane `extractelement` off `GEP` -- the scalar address
          // `FunctionWidener::widenGroupSharedAtomicRMW`/
          // `widenGroupSharedAtomicCmpXchg` extract, one per lane, out of
          // a genuinely divergent (already-widened vector-of-pointers)
          // address, since neither `atomicrmw` nor `cmpxchg` (unlike a
          // `load`/`store`) has a real vector-of-pointers gather/scatter
          // form to widen into directly -- is supported too, as long as
          // every one of the `extractelement`'s own users is an ordinary
          // scalar leaf access (in practice always a per-lane
          // `atomicrmw`/`cmpxchg` clone).
          if (auto *EE = dyn_cast<ExtractElementInst>(GEPUser);
              EE && GEP->getType()->isVectorTy() &&
              llvm::all_of(EE->users(), isSupportedGroupSharedLeafUser))
            continue;
          Ctx.emitError(
              "feme-cpu-simdize: groupshared global '" + GV->getName() +
              "' feeds an unsupported user; only a getelementptr chain "
              "(to any depth) feeding a direct load, store, atomicrmw, "
              "masked gather/scatter, pointer compare, pointer merge "
              "(select/phi), or (for a vector-typed row load, or a "
              "uniform row address broadcast into one) an extractelement "
              "feeding its own scalar leaf access is supported (roadmap "
              "milestone 9 deviation)");
          return false;
        }
      }
    }
  }

  // (Roadmap L430) Validation above has already confirmed every merge/
  // compare in `F` is supported; retarget them now, before the ordinary
  // per-global rewrite loop below -- see
  // `retargetGroupSharedMergesAndCompares`'s own comment for why this
  // must run first (a merge may combine legs rooted at two different
  // globals, each otherwise retargeted independently by a separate
  // iteration of that loop).
  retargetGroupSharedMergesAndCompares(F, GroupSharedBase, Layout);

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
