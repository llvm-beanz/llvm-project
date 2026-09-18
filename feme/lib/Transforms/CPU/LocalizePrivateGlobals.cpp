//===- LocalizePrivateGlobals.cpp - Localize per-invocation globals ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/LocalizePrivateGlobals.h"

#include "feme/Core/StageOps.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Whether every leaf of \p Ty (a scalar, or a `FixedVectorType`, once
/// every nested `StructType`/`ArrayType` level is stripped away) is a
/// shape this pass's new aggregate-global localization (roadmap
/// L116(a)/C8b) can safely hand off to `feme::cpu::PreparePass`'s own
/// `SROAPass` (which runs immediately after this pass in the pipeline,
/// see `feme::cpu::runPipeline`'s own ordering) without reintroducing the
/// exact memory-layout corruption `feme::cpu::LocalNarrowVectorArrayInitPass`
/// exists to prevent: a `FixedVectorType` leaf is only accepted here if
/// its real, ABI-padded `DataLayout::getTypeAllocSize` already agrees with
/// its tightly packed (no inter-element padding) size -- i.e. a
/// non-power-of-2-width vector element (e.g. `<3 x float>`, padded to 16
/// bytes instead of 12) is deliberately rejected, since some *other* user
/// of the same global may still assume the tight, non-ABI offset that
/// pass's own fixup exists to preserve, and this pass runs before it
/// would ever have a chance to normalize that mismatch away. A vector
/// whose width is already a power of 2 (as most are) has no such gap: its
/// tight and ABI-padded sizes always coincide.
bool isSupportedAggregateLeafType(Type *Ty, const DataLayout &DL) {
  if (auto *StructTy = dyn_cast<StructType>(Ty))
    return llvm::all_of(StructTy->elements(), [&](Type *ElTy) {
      return isSupportedAggregateLeafType(ElTy, DL);
    });
  if (auto *ArrayTy = dyn_cast<ArrayType>(Ty))
    return isSupportedAggregateLeafType(ArrayTy->getElementType(), DL);
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    uint64_t TightSize =
        VecTy->getNumElements() * DL.getTypeAllocSize(VecTy->getElementType());
    return TightSize == DL.getTypeAllocSize(VecTy);
  }
  return !Ty->isVectorTy();
}

/// Whether \p GV is a candidate for localization: address space 0 (the
/// `Private`/`Function`-storage convention `feme::cpu::
/// LocalNarrowVectorArrayInitPass`'s own header comment documents), a
/// real (non-external) definition, mutable (excludes e.g. a debug-name
/// string literal `llvm.mlir.global constant` a diagnostic call might
/// reference), and either a scalar/fixed-vector value type outright, or
/// (roadmap L116(a)/C8b) a struct/array whose every leaf is one of those
/// same shapes and safe to hand to `SROAPass` -- see
/// `isSupportedAggregateLeafType`'s own comment for the one narrow
/// exclusion (a non-power-of-2-width vector leaf) that still applies.
bool isCandidateGlobal(GlobalVariable &GV) {
  if (GV.getAddressSpace() != 0 || GV.isDeclaration() || GV.isConstant())
    return false;
  Type *ValTy = GV.getValueType();
  if (ValTy->isSingleValueType())
    return true;
  return isSupportedAggregateLeafType(ValTy, GV.getParent()->getDataLayout());
}


/// Whether \p F contains a `feme.stage.discard`/`.demote` call, directly
/// or in any function it calls (transitively): once either fires, every
/// later memory access in the invocation becomes conditionally executed
/// (`feme::cpu::LinearizePass` narrows the side-effect mask going
/// forward), even where the condition is itself provably wave-uniform --
/// e.g. driven only by a uniform-buffer read, never `gl_FragCoord` or
/// similar per-invocation state, so every lane in a wave necessarily
/// agrees on it at runtime. `feme::cpu::SIMDizePass`'s own "a value this
/// pass's own analysis already proved is uniform" leftover-use recovery
/// (see `FunctionWidener::widen`'s cleanup loop) currently assumes lane 0
/// of a `MaskedAllocas`-widened per-lane load is always a valid
/// representative -- true for a masked read of a genuinely shared
/// address, but not guaranteed once this pass gives every lane its own
/// separate storage and a governing mask can (even only in principle)
/// leave some lane's own copy unwritten. Found reducing a real regression
/// this row's own aggregate-global broadening (roadmap L116(a)/C8b)
/// caused in `dEQP-VK.graphicsfuzz.cov-function-loop-condition-constant-
/// array-always-false` (whose `data0`/`data1` file-scope `int` arrays are
/// mutated inside a function containing exactly this shape: an always-
/// false, but not statically foldable, `discard`). Conservatively
/// excluding any global used inside a function that could ever discard
/// or demote sidesteps this gap entirely, at the cost of leaving such an
/// aggregate global unlocalized (its pre-existing, working "one shared
/// address" behavior) until `SIMDizePass`'s own leftover-use recovery is
/// taught to verify real per-lane validity instead of assuming it -- left
/// for a future session, since that is a deeper, separate `SIMDize.cpp`
/// fix, not scoped to this pass.
bool mayDiscardOrDemote(Function &F) {
  SmallPtrSet<Function *, 8> Visited;
  SmallVector<Function *, 8> Worklist{&F};
  while (!Worklist.empty()) {
    Function *Cur = Worklist.pop_back_val();
    if (!Visited.insert(Cur).second || Cur->isDeclaration())
      continue;
    for (Instruction &I : instructions(*Cur)) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      Function *Callee = CI->getCalledFunction();
      if (!Callee)
        continue;
      if (std::optional<feme::StageOpKind> Kind = feme::getStageOpKind(*Callee)) {
        if (*Kind == feme::StageOpKind::Discard ||
            *Kind == feme::StageOpKind::Demote)
          return true;
        continue;
      }
      Worklist.push_back(Callee);
    }
  }
  return false;
}

/// Whether every user of \p GV is an `Instruction` belonging to the same
/// single `Function`, returned through \p OnlyFunc -- i.e. \p GV is safe
/// to replace with a local `alloca` in that one function, rather than a
/// genuinely cross-function-shared global this pass must leave alone.
bool usedByOneFunctionOnly(GlobalVariable &GV, Function *&OnlyFunc) {
  OnlyFunc = nullptr;
  for (User *U : GV.users()) {
    auto *I = dyn_cast<Instruction>(U);
    if (!I)
      return false;
    Function *F = I->getFunction();
    if (!OnlyFunc)
      OnlyFunc = F;
    else if (OnlyFunc != F)
      return false;
  }
  return OnlyFunc != nullptr;
}

} // namespace

PreservedAnalyses LocalizePrivateGlobalsPass::run(Module &M,
                                                   ModuleAnalysisManager &) {
  bool Changed = false;
  for (GlobalVariable &GV : make_early_inc_range(M.globals())) {
    if (!isCandidateGlobal(GV))
      continue;
    Function *OnlyFunc = nullptr;
    if (!usedByOneFunctionOnly(GV, OnlyFunc))
      continue;
    // See `mayDiscardOrDemote`'s own comment: an aggregate-typed global
    // is only localized when its one using function can never discard or
    // demote, sidestepping a separate, pre-existing `SIMDizePass` gap
    // this broadening would otherwise expose. A scalar/fixed-vector
    // global was already being localized before roadmap L116(a)/C8b and
    // is unaffected by this guard.
    if (!GV.getValueType()->isSingleValueType() &&
        mayDiscardOrDemote(*OnlyFunc))
      continue;

    BasicBlock &Entry = OnlyFunc->getEntryBlock();
    IRBuilder<> B(&Entry, Entry.getFirstInsertionPt());
    AllocaInst *AI =
        B.CreateAlloca(GV.getValueType(), /*ArraySize=*/nullptr, GV.getName());
    // An `undef` initializer (the common case for a SPIR-V `Private`
    // variable with no explicit GLSL/HLSL initializer) needs no seeding
    // store at all; a real initializer must still run once, up front,
    // exactly as the original global's own implicit "already holds this
    // value before the entry function's first instruction" semantics did.
    if (GV.hasInitializer() && !isa<UndefValue>(GV.getInitializer()))
      B.CreateStore(GV.getInitializer(), AI);

    GV.replaceAllUsesWith(AI);
    GV.eraseFromParent();
    Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
