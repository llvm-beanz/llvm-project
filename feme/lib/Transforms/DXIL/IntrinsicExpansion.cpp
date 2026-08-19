//===- IntrinsicExpansion.cpp - Expand llvm.dx.* math intrinsics ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/IntrinsicExpansion.h"

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::dxil;

namespace {

/// Builds the dot product of the two \p N-component vectors DXIL's `Dot2`..
/// `Dot4` ops pass as 2*N interleaved scalar operands (all of a, then all of
/// b), as a chain of `llvm.fmuladd` calls so the expansion keeps the
/// intrinsic's own fused-multiply-add semantics.
Value *expandDot(IRBuilder<> &Builder, CallInst &CI, unsigned N) {
  if (CI.arg_size() != 2 * N)
    return nullptr;
  Type *Ty = CI.getType();
  Function *FMulAdd =
      Intrinsic::getOrInsertDeclaration(CI.getModule(), Intrinsic::fmuladd, Ty);

  Value *Result = Builder.CreateFMul(CI.getArgOperand(0), CI.getArgOperand(N));
  for (unsigned I = 1; I != N; ++I)
    Result = Builder.CreateCall(
        FMulAdd, {CI.getArgOperand(I), CI.getArgOperand(N + I), Result});
  return Result;
}

/// Builds the dot product of `llvm.dx.fdot`'s two vector operands (SM6.9's
/// unified, arity-agnostic replacement for `Dot2`..`Dot4`, see the `FDot`
/// comment in OpRaising.cpp) as a chain of `llvm.fmuladd` calls over each
/// lane, the same way `expandDot` does for its interleaved-scalar-operand
/// predecessors. Returns nullptr if \p CI's first operand isn't a fixed
/// vector, which is the only shape `int_dx_fdot` is ever raised with.
Value *expandFDot(IRBuilder<> &Builder, CallInst &CI) {
  Value *A = CI.getArgOperand(0);
  Value *B = CI.getArgOperand(1);
  auto *VecTy = dyn_cast<FixedVectorType>(A->getType());
  if (!VecTy)
    return nullptr;

  unsigned N = VecTy->getNumElements();
  Type *ElemTy = VecTy->getElementType();
  Function *FMulAdd = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::fmuladd, ElemTy);

  Value *Result =
      Builder.CreateFMul(Builder.CreateExtractElement(A, uint64_t(0)),
                         Builder.CreateExtractElement(B, uint64_t(0)));
  for (unsigned I = 1; I != N; ++I) {
    Value *Ai = Builder.CreateExtractElement(A, I);
    Value *Bi = Builder.CreateExtractElement(B, I);
    Result = Builder.CreateCall(FMulAdd, {Ai, Bi, Result});
  }
  return Result;
}

/// Returns the expansion of \p CI, a call to the `llvm.dx.*` intrinsic \p ID,
/// or nullptr if this pass has no context-free definition for it.
Value *expandCall(IRBuilder<> &Builder, CallInst &CI, Intrinsic::ID ID) {
  Type *Ty = CI.getType();
  Module &M = *CI.getModule();

  switch (ID) {
  case Intrinsic::dx_frac: {
    // HLSL's frac() is the *floor*-relative fractional part, so it is
    // positive for negative inputs too -- unlike `llvm.trunc`-based
    // truncation towards zero.
    Function *Floor =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::floor, Ty);
    Value *Whole = Builder.CreateCall(Floor, {CI.getArgOperand(0)});
    return Builder.CreateFSub(CI.getArgOperand(0), Whole);
  }
  case Intrinsic::dx_saturate: {
    // saturate(x) == clamp(x, 0, 1). `maxnum`/`minnum` (rather than
    // `fcmp`+`select`) match DXIL's own NaN behaviour: saturate(NaN) is 0.
    Function *MaxNum =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::maxnum, Ty);
    Function *MinNum =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::minnum, Ty);
    Value *Low = Builder.CreateCall(
        MaxNum, {CI.getArgOperand(0), ConstantFP::get(Ty, 0.0)});
    return Builder.CreateCall(MinNum, {Low, ConstantFP::get(Ty, 1.0)});
  }
  case Intrinsic::dx_rsqrt: {
    Function *Sqrt = Intrinsic::getOrInsertDeclaration(&M, Intrinsic::sqrt, Ty);
    Value *Root = Builder.CreateCall(Sqrt, {CI.getArgOperand(0)});
    return Builder.CreateFDiv(ConstantFP::get(Ty, 1.0), Root);
  }
  case Intrinsic::dx_imad:
  case Intrinsic::dx_umad: {
    // Signed and unsigned multiply-add differ only in the overflow flags
    // DXIL's own ops carry, which plain `mul`/`add` do not claim at all.
    Value *Product =
        Builder.CreateMul(CI.getArgOperand(0), CI.getArgOperand(1));
    return Builder.CreateAdd(Product, CI.getArgOperand(2));
  }
  case Intrinsic::dx_dot2:
    return expandDot(Builder, CI, 2);
  case Intrinsic::dx_dot3:
    return expandDot(Builder, CI, 3);
  case Intrinsic::dx_dot4:
    return expandDot(Builder, CI, 4);
  case Intrinsic::dx_fdot:
    return expandFDot(Builder, CI);
  case Intrinsic::dx_isinf:
  case Intrinsic::dx_isnan: {
    Function *IsFPClass = Intrinsic::getOrInsertDeclaration(
        &M, Intrinsic::is_fpclass, {CI.getArgOperand(0)->getType()});
    FPClassTest Mask = ID == Intrinsic::dx_isinf ? fcInf : fcNan;
    return Builder.CreateCall(IsFPClass,
                              {CI.getArgOperand(0), Builder.getInt32(Mask)});
  }
  default:
    return nullptr;
  }
}

} // namespace

PreservedAnalyses IntrinsicExpansionPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    Intrinsic::ID ID = F.getIntrinsicID();
    if (ID == Intrinsic::not_intrinsic)
      continue;

    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;

      IRBuilder<> Builder(CI);
      if (isa<FPMathOperator>(CI))
        Builder.setFastMathFlags(CI->getFastMathFlags());
      Value *Expansion = expandCall(Builder, *CI, ID);
      if (!Expansion)
        continue;

      Expansion->takeName(CI);
      CI->replaceAllUsesWith(Expansion);
      CI->eraseFromParent();
      Changed = true;
    }

    if (F.use_empty() && F.isDeclaration())
      F.eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
