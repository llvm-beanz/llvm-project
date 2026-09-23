//===- SPIRVUnmergeResourceLoads.cpp - Split phi-merged resource loads ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See SPIRVUnmergeResourceLoads.h for the full rationale (roadmap
// L174/L155).
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVUnmergeResourceLoads.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"

#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Returns true if \p V is a direct call to `llvm.spv.resource.getpointer`.
bool isSPIRVResourceGetPointerCall(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee &&
         Callee->getIntrinsicID() == Intrinsic::spv_resource_getpointer;
}

/// Returns true if every instruction in \p BB strictly after \p After (and
/// before \p BB's terminator) is free of any memory-write side effect --
/// i.e. hoisting a `load` that currently reads through \p BB's incoming
/// edge back to immediately after \p After cannot observe a different
/// value than the `load` it is replacing does today.
bool isSafeToHoistLoadAfter(const Instruction *After) {
  for (const Instruction *I = After->getNextNode(); I && !I->isTerminator();
       I = I->getNextNode()) {
    if (I->mayWriteToMemory())
      return false;
  }
  return true;
}

/// If \p To is reachable from \p From via a single, unbranched chain of
/// blocks -- i.e. \p To, and every block between it and \p From, has
/// exactly one predecessor, so \p From provably dominates \p To and no
/// other control-flow path can reach it -- returns that chain in forward
/// order (excluding \p From, including \p To). Returns an empty chain if
/// \p From == \p To (no cross-block hoisting needed at all). Returns
/// `std::nullopt` if \p To is reachable only via more than one
/// control-flow path (this pass has no way to re-merge a value at such a
/// join point) or not reachable from \p From by a forward chain at all.
///
/// This is deliberately a narrower, cheaper check than a full
/// `DominatorTree` query: every real-world shape this pass has needed to
/// handle so far (roadmap L175/L176's `vertex_fragment` sunk load: an
/// outer `if`'s single-predecessor arm) is exactly this "linear
/// extension" shape, and restricting to it keeps the safety argument
/// below (see `isPathFreeOfWrites`) a simple straight-line walk rather
/// than a full multi-path reachability analysis.
std::optional<SmallVector<BasicBlock *, 4>> findLinearChainTo(BasicBlock *From,
                                                               BasicBlock *To) {
  if (From == To)
    return SmallVector<BasicBlock *, 4>{};

  SmallVector<BasicBlock *, 4> Chain;
  BasicBlock *Cur = To;
  // Bound the backward walk by the function's own block count: this
  // guards against an (unexpected, but not impossible) cycle in the
  // "unique predecessor" chain that never actually reaches `From`.
  for (unsigned Bound = From->getParent()->size(); Cur != From; --Bound) {
    if (Bound == 0)
      return std::nullopt;
    Chain.push_back(Cur);
    BasicBlock *Pred = Cur->getUniquePredecessor();
    if (!Pred)
      return std::nullopt;
    Cur = Pred;
  }
  std::reverse(Chain.begin(), Chain.end());
  return Chain;
}

/// Returns true if nothing between \p PN (exclusive) and \p LI (exclusive)
/// -- first the rest of \p PN's own parent block, then each block of
/// \p Chain in turn, the last of which contains \p LI itself -- may write
/// to memory. This is the cross-block generalization of
/// `isSafeToHoistLoadAfter`: it proves that computing \p LI's value as
/// early as immediately after each incoming block's own `getpointer` call
/// (already checked safe *within* that incoming block by
/// `isSafeToHoistLoadAfter`) remains safe all the way through to \p LI's
/// original site, however many extra blocks of \p Chain that site was
/// sunk across.
bool isPathFreeOfWrites(const PHINode &PN, ArrayRef<BasicBlock *> Chain,
                        const LoadInst *LI) {
  for (const Instruction *I = PN.getNextNode(); I; I = I->getNextNode()) {
    if (I == LI)
      return true;
    if (I->mayWriteToMemory())
      return false;
    if (I->isTerminator())
      break;
  }

  for (const BasicBlock *BB : Chain) {
    for (const Instruction &I : *BB) {
      if (&I == LI)
        return true;
      if (I.mayWriteToMemory())
        return false;
    }
  }
  llvm_unreachable("LI must be reachable via PN's own block or Chain");
}

/// If \p PN is a pointer-typed `PHINode` all of whose incoming values are
/// `llvm.spv.resource.getpointer` calls each residing in their own
/// incoming block (with nothing but side-effect-free instructions between
/// that call and the block's terminator), splits every `load` using \p PN
/// as its pointer operand back into one `load` per incoming block plus a
/// new `PHINode` of the loaded values, and returns true. Otherwise leaves
/// \p PN untouched and returns false.
bool tryUnmergeResourcePointerPHI(PHINode &PN) {
  if (!PN.getType()->isPointerTy())
    return false;

  for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
    Value *InVal = PN.getIncomingValue(I);
    BasicBlock *InBB = PN.getIncomingBlock(I);
    if (!isSPIRVResourceGetPointerCall(InVal))
      return false;
    auto *GetPtr = cast<Instruction>(InVal);
    if (GetPtr->getParent() != InBB || !isSafeToHoistLoadAfter(GetPtr))
      return false;
  }

  // Collect the `load`s using this PHI as their pointer operand before
  // mutating anything -- rewriting invalidates `PN`'s use-list iterators.
  //
  // A `load` need not live in `PN`'s own parent block: it may have been
  // sunk into a later block by an earlier pass (e.g. an outer, unrelated
  // `if` that only uses the loaded value along one path -- the
  // `vertex_fragment`-stage shape roadmap L175/L176 found). That is only
  // safe to rewrite if that later block is reachable from `PN`'s parent
  // via a single, unbranched chain of blocks (`findLinearChainTo`): this
  // proves `PN`'s parent provably dominates the load's block, so the
  // merged value this pass builds there is well-defined at the load's
  // site too, without needing to rebuild any further merge along the
  // way. A load reachable only via more than one control-flow path (a
  // second, independent join downstream of this one) is left untouched
  // -- this pass has no way to re-merge a value at that second join
  // point too.
  struct PendingLoad {
    LoadInst *LI;
    SmallVector<BasicBlock *, 4> Chain;
  };
  SmallVector<PendingLoad, 4> Loads;
  for (User *U : PN.users()) {
    auto *LI = dyn_cast<LoadInst>(U);
    if (!LI || LI->getPointerOperand() != &PN)
      continue;
    std::optional<SmallVector<BasicBlock *, 4>> Chain =
        findLinearChainTo(PN.getParent(), LI->getParent());
    if (!Chain || !isPathFreeOfWrites(PN, *Chain, LI))
      continue;
    Loads.push_back({LI, std::move(*Chain)});
  }
  if (Loads.empty())
    return false;

  bool Changed = false;
  for (PendingLoad &Pending : Loads) {
    LoadInst *LI = Pending.LI;
    PHINode *ValuePHI = PHINode::Create(LI->getType(), PN.getNumIncomingValues(),
                                         LI->getName() + ".unmerged");
    // Insert at `PN`'s own position, not `LI`'s: `PN` is a `PHINode`, so
    // (the input IR being valid) it is guaranteed to already sit among
    // the block's leading run of phis. `LI` is not -- when this block
    // holds more than one independent phi-of-pointer merge (e.g. a
    // second switch's own resource access sharing a store/merge block
    // with an already-lowered value from an earlier one), a non-phi
    // instruction can legally sit between `PN` and its own `LI`, and
    // inserting a new phi there would violate the "phis grouped at the
    // top of the block" invariant -- exactly the invalid-IR shape found
    // via CTS's `binding_model.shader_access.*multiple_descriptor_sets*`
    // (roadmap L175/L177).
    ValuePHI->insertBefore(PN.getIterator());
    for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
      Value *InVal = PN.getIncomingValue(I);
      BasicBlock *InBB = PN.getIncomingBlock(I);
      auto *ClonedLoad = cast<LoadInst>(LI->clone());
      ClonedLoad->setOperand(LoadInst::getPointerOperandIndex(), InVal);
      ClonedLoad->setName(LI->getName() + ".unmerged.in");
      ClonedLoad->insertAfter(cast<Instruction>(InVal));
      ValuePHI->addIncoming(ClonedLoad, InBB);
    }
    // `ValuePHI` dominates `LI`'s own site whether or not they share a
    // block: same-block, this is the ordinary "phi feeds a load further
    // down the same block" case; cross-block, `Pending.Chain` already
    // proved `PN`'s (and so `ValuePHI`'s) parent block is the sole route
    // to `LI`'s block, which is exactly the dominance this relies on.
    LI->replaceAllUsesWith(ValuePHI);
    LI->eraseFromParent();
    Changed = true;
  }

  if (PN.use_empty())
    PN.eraseFromParent();

  return Changed;
}

} // namespace

PreservedAnalyses SPIRVUnmergeResourceLoadsPass::run(Module &M,
                                                      ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    // Collect first: `tryUnmergeResourcePointerPHI` erases the `PHINode`
    // it successfully rewrites, and may insert new ones, so iterating
    // `F`'s instructions directly while rewriting is unsafe.
    SmallVector<PHINode *, 8> Candidates;
    for (Instruction &I : instructions(F))
      if (auto *PN = dyn_cast<PHINode>(&I); PN && PN->getType()->isPointerTy())
        Candidates.push_back(PN);

    for (PHINode *PN : Candidates)
      Changed |= tryUnmergeResourcePointerPHI(*PN);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
