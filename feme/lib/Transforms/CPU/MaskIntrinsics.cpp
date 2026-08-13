//===- MaskIntrinsics.cpp - `feme.cpu.mask.*` call helpers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MaskIntrinsics.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

Function *feme::cpu::getOrInsertMaskAny(Module &M) {
  LLVMContext &Ctx = M.getContext();
  Type *I1Ty = Type::getInt1Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I1Ty, {I1Ty}, /*isVarArg=*/false);
  Function *F = cast<Function>(
      M.getOrInsertFunction("feme.cpu.mask.any", FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    F->setMemoryEffects(MemoryEffects::none());
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

CallInst *feme::cpu::createMaskAny(IRBuilderBase &Builder, Value *Mask,
                                   const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  return Builder.CreateCall(getOrInsertMaskAny(*M), {Mask}, Name);
}
