//===- LocalizePrivateGlobals.cpp - Localize per-invocation globals ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/LocalizePrivateGlobals.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// Whether \p GV is a candidate for localization: address space 0 (the
/// `Private`/`Function`-storage convention `feme::cpu::
/// LocalNarrowVectorArrayInitPass`'s own header comment documents), a
/// real (non-external) definition, mutable (excludes e.g. a debug-name
/// string literal `llvm.mlir.global constant` a diagnostic call might
/// reference), and a scalar or fixed-vector element type -- an
/// array/struct is deliberately left alone here; see this pass's own
/// header comment for why.
bool isCandidateGlobal(GlobalVariable &GV) {
  if (GV.getAddressSpace() != 0 || GV.isDeclaration() || GV.isConstant())
    return false;
  Type *ValTy = GV.getValueType();
  return ValTy->isSingleValueType();
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
