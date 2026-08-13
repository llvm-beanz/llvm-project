//===- BoundResourceNormalization.cpp - Bound-resource-to-heap emulation ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/BoundResourceNormalization.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <limits>
#include <map>
#include <optional>
#include <tuple>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Which of the two resource kinds `feme::cpu::ResourceLoweringPass`
/// canonicalizes a bound handle belongs to (see its own `Family`); needed
/// here only to reject a conflicting re-declaration of the same binding,
/// never surfaced beyond that.
enum class Family { Typed, Raw };

/// A bound handle's identity, per "Bound-resource normalization": source
/// model (always DXIL for now, see the header comment), register space,
/// base register, and the resource kind/array length recovered from the
/// call itself.
struct RangeKey {
  uint32_t Space;
  uint32_t LowerBound;

  bool operator<(const RangeKey &Other) const {
    return std::tie(Space, LowerBound) <
           std::tie(Other.Space, Other.LowerBound);
  }
};

/// One `handlefrombinding` call this pass will rewrite, plus its identity.
struct BoundHandle {
  CallInst *Handle;
  RangeKey Key;
  Family Kind;
  uint32_t RangeSize;
};

/// The outcome of collecting one (space, register) identity's uses: either
/// a single, consistent range, or a conflict/unbounded range that leaves
/// every call at that identity un-normalized (see the header comment).
struct RangeEntry {
  Family Kind;
  uint32_t RangeSize = 0;
  bool Conflicting = false;
  /// Assigned once every range has been collected (see `assignHeapBases`).
  uint32_t HeapBase = 0;
};

/// Classifies \p Handle's resource kind from its `target("dx.")` handle
/// type, mirroring `classifyHandle` in ResourceLowering.cpp -- only the
/// same two kinds that pass canonicalizes are normalized here (see the
/// header comment).
std::optional<Family> classifyFamily(const CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy)
    return std::nullopt;
  if (HandleTy->getName() == "dx.TypedBuffer")
    return Family::Typed;
  if (HandleTy->getName() == "dx.RawBuffer")
    return Family::Raw;
  return std::nullopt;
}

/// Collects every normalizable `handlefrombinding` call in \p M, and folds
/// each (space, register) identity's uses into one `RangeEntry` --
/// unbounded (`RangeSize == 0`) or mutually-inconsistent identities are
/// marked `Conflicting` so `rewriteBoundHandles` leaves them alone.
void collectBoundHandles(Module &M, SmallVectorImpl<BoundHandle> &Handles,
                         std::map<RangeKey, RangeEntry> &Ranges) {
  for (Function &F : M) {
    if (F.getIntrinsicID() != Intrinsic::dx_resource_handlefrombinding)
      continue;
    for (User *U : F.users()) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;
      std::optional<Family> Kind = classifyFamily(*CI);
      if (!Kind)
        continue; // Not one of the two kinds this milestone normalizes.

      auto *SpaceC = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      auto *LowerBoundC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
      auto *RangeSizeC = dyn_cast<ConstantInt>(CI->getArgOperand(2));
      if (!SpaceC || !LowerBoundC || !RangeSizeC)
        continue; // Non-constant binding: not produced by any raiser today.

      RangeKey Key{static_cast<uint32_t>(SpaceC->getZExtValue()),
                   static_cast<uint32_t>(LowerBoundC->getZExtValue())};
      uint32_t RangeSize = static_cast<uint32_t>(RangeSizeC->getZExtValue());

      auto It = Ranges.find(Key);
      if (It == Ranges.end()) {
        Ranges.emplace(Key, RangeEntry{*Kind, RangeSize,
                                       /*Conflicting=*/RangeSize == 0});
      } else if (It->second.Kind != *Kind ||
                 It->second.RangeSize != RangeSize) {
        It->second.Conflicting = true;
      }

      Handles.push_back(BoundHandle{CI, Key, *Kind, RangeSize});
    }
  }
}

/// Assigns each non-conflicting range a contiguous base slot in the
/// reserved heap prefix, sorted by identity (space, then register) for a
/// deterministic (if not itself ABI-guaranteed) layout -- see
/// "Bound-resource normalization"'s step 2. Returns the total reserved
/// prefix size.
uint32_t assignHeapBases(std::map<RangeKey, RangeEntry> &Ranges) {
  uint32_t Base = 0;
  for (auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    Entry.HeapBase = Base;
    Base += Entry.RangeSize;
  }
  return Base;
}

/// Builds `select(Base + Index > UINT32_MAX, UINT32_MAX, Base + Index)`,
/// computed in i64 so the overflow itself can be detected exactly.
Value *computeOverflowClampedIndex(IRBuilderBase &Builder, Value *Index,
                                   uint32_t Base) {
  LLVMContext &Ctx = Builder.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);

  Value *Sum64 = Builder.CreateAdd(ConstantInt::get(I64Ty, Base),
                                   Builder.CreateZExt(Index, I64Ty));
  Value *Overflow = Builder.CreateICmpUGT(
      Sum64, ConstantInt::get(I64Ty, std::numeric_limits<uint32_t>::max()));
  return Builder.CreateSelect(
      Overflow, ConstantInt::get(I32Ty, std::numeric_limits<uint32_t>::max()),
      Builder.CreateTrunc(Sum64, I32Ty));
}

/// Builds `select(OutOfRange, UINT32_MAX, Base + Index)`, using
/// `computeOverflowClampedIndex` for the addition itself (see
/// "Bound-resource normalization"'s step 3): `Index` is unsigned and
/// compared against \p RangeSize first, so only a range this large ever
/// exercises the overflow path in practice, but the design requires both
/// checks.
Value *computeClampedIndex(IRBuilderBase &Builder, Value *Index, uint32_t Base,
                           uint32_t RangeSize) {
  Type *I32Ty = Type::getInt32Ty(Builder.getContext());
  Value *OutOfRange =
      Builder.CreateICmpUGE(Index, ConstantInt::get(I32Ty, RangeSize));
  Value *Clamped = computeOverflowClampedIndex(Builder, Index, Base);
  return Builder.CreateSelect(
      OutOfRange, ConstantInt::get(I32Ty, std::numeric_limits<uint32_t>::max()),
      Clamped);
}

/// Rewrites every accepted `BoundHandle` into the corresponding
/// `handlefromheap` call, returning the set of functions touched.
void rewriteBoundHandles(const SmallVectorImpl<BoundHandle> &Handles,
                         const std::map<RangeKey, RangeEntry> &Ranges,
                         SmallPtrSetImpl<Function *> &TouchedFunctions) {
  for (const BoundHandle &BH : Handles) {
    const RangeEntry &Entry = Ranges.at(BH.Key);
    if (Entry.Conflicting)
      continue;

    IRBuilder<> Builder(BH.Handle);
    Value *Index = BH.Handle->getArgOperand(3);
    Value *NewIndex =
        computeClampedIndex(Builder, Index, Entry.HeapBase, BH.RangeSize);

    Function *HeapFn = Intrinsic::getOrInsertDeclaration(
        BH.Handle->getModule(), Intrinsic::dx_resource_handlefromheap,
        {BH.Handle->getType()});
    Value *NewCall = Builder.CreateCall(
        HeapFn, {NewIndex, Builder.getInt1(false)}, BH.Handle->getName());
    BH.Handle->replaceAllUsesWith(NewCall);
    TouchedFunctions.insert(cast<Instruction>(NewCall)->getFunction());
    BH.Handle->eraseFromParent();
  }
}

/// Adds \p PrefixSize to every *native* (not created by this pass)
/// `handlefromheap` call's index operand, so a shader mixing traditional
/// and bindless resources keeps its own dynamic heap indices unambiguous
/// (see "Bound-resource normalization"'s step 4). Only the same two
/// resource kinds this pass normalizes are offset, matching scope.
void offsetNativeDynamicIndices(Module &M, uint32_t PrefixSize,
                                SmallPtrSetImpl<Function *> &TouchedFunctions) {
  if (PrefixSize == 0)
    return;

  for (Function &F : M) {
    if (F.getIntrinsicID() != Intrinsic::dx_resource_handlefromheap)
      continue;
    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;
      if (!classifyFamily(*CI))
        continue;

      IRBuilder<> Builder(CI);
      Value *NewIndex = computeOverflowClampedIndex(
          Builder, CI->getArgOperand(0), PrefixSize);
      CI->setArgOperand(0, NewIndex);
      TouchedFunctions.insert(CI->getFunction());
    }
  }
}

/// Attaches the `!feme.cpu.bound_resources` metadata node "Publishing"
/// (see the design doc's "Bound-resource normalization"/"Descriptor
/// heaps" sections) describes for \p F: its name, the reserved resource
/// heap prefix size, then each accepted range as a (space, register,
/// range-size, heap-base) tuple, sorted the same deterministic way
/// `assignHeapBases` assigned them.
void attachBoundResourceMetadata(Function &F, uint32_t PrefixSize,
                                 const std::map<RangeKey, RangeEntry> &Ranges) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  SmallVector<Metadata *, 8> Ops;
  Ops.push_back(MDString::get(Ctx, F.getName()));
  Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, PrefixSize)));
  for (const auto &[Key, Entry] : Ranges) {
    if (Entry.Conflicting)
      continue;
    Ops.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, Key.Space)));
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Key.LowerBound)));
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Entry.RangeSize)));
    Ops.push_back(
        ConstantAsMetadata::get(ConstantInt::get(I32Ty, Entry.HeapBase)));
  }

  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.bound_resources")
      ->addOperand(MDNode::get(Ctx, Ops));
}

} // namespace

PreservedAnalyses BoundResourceNormalizationPass::run(Module &M,
                                                      ModuleAnalysisManager &) {
  SmallVector<BoundHandle, 4> Handles;
  std::map<RangeKey, RangeEntry> Ranges;
  collectBoundHandles(M, Handles, Ranges);
  if (Handles.empty())
    return PreservedAnalyses::all();

  uint32_t PrefixSize = assignHeapBases(Ranges);

  SmallPtrSet<Function *, 4> TouchedFunctions;
  // Offset every *native* `handlefromheap` call first, while it is still
  // unambiguous which calls those are -- `rewriteBoundHandles` below creates
  // new `handlefromheap` calls of its own, which must not be offset again.
  offsetNativeDynamicIndices(M, PrefixSize, TouchedFunctions);
  rewriteBoundHandles(Handles, Ranges, TouchedFunctions);

  if (TouchedFunctions.empty())
    return PreservedAnalyses::all(); // Every identity conflicted/unbounded.

  for (Function *F : TouchedFunctions)
    attachBoundResourceMetadata(*F, PrefixSize, Ranges);

  // An unused `handlefrombinding` declaration is left behind once its last
  // (accepted) caller is rewritten away; a conflicting/unbounded one may
  // still have users, and is left for `checkSupportedRaisedOps` to reject.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getIntrinsicID() == Intrinsic::dx_resource_handlefrombinding)
      F.eraseFromParent();

  return PreservedAnalyses::none();
}
