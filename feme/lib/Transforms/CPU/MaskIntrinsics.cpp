//===- MaskIntrinsics.cpp - `feme.cpu.mask.*` call helpers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/MaskIntrinsics.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// Appends the scalar type mangling a `feme.cpu.masked.load`/`.store` name
/// uses, e.g. `f32`, `i32`. Mirrors `feme::cpu::mangleResourceCallName`'s
/// scalar mangling (Transforms/CPU/ResourceCalls.cpp), duplicated here
/// rather than shared: the two families mangle unrelated declarations, and
/// the vector-element case that file's version also handles never arises
/// for a masked load/store operand, which is always the memory access's
/// direct scalar/vector type, not a further-decomposed component.
void appendScalarMangling(raw_ostream &OS, Type *Ty) {
  if (Ty->isHalfTy()) {
    OS << "f16";
  } else if (Ty->isFloatTy()) {
    OS << "f32";
  } else if (Ty->isDoubleTy()) {
    OS << "f64";
  } else if (Ty->isIntegerTy()) {
    OS << "i" << Ty->getIntegerBitWidth();
  } else if (Ty->isPointerTy()) {
    OS << "p0";
  } else {
    llvm_unreachable("unsupported feme.cpu.masked.* element type");
  }
}

std::string mangleMaskedMemOpName(StringRef Prefix, Type *ElementType) {
  std::string Name;
  raw_string_ostream OS(Name);
  OS << Prefix;
  if (auto *VecTy = dyn_cast<FixedVectorType>(ElementType)) {
    OS << 'v' << VecTy->getNumElements();
    appendScalarMangling(OS, VecTy->getElementType());
  } else {
    appendScalarMangling(OS, ElementType);
  }
  return Name;
}

} // namespace

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

bool feme::cpu::isMaskAnyCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  return Callee && Callee->getName() == "feme.cpu.mask.any";
}

Function *feme::cpu::getOrInsertMaskedLoad(Module &M, Type *ElementType) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  FunctionType *FTy = FunctionType::get(
      ElementType, {PtrTy, I32Ty, I1Ty, ElementType}, /*isVarArg=*/false);
  std::string Name = mangleMaskedMemOpName("feme.cpu.masked.load.", ElementType);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    F->setMemoryEffects(MemoryEffects::argMemOnly(ModRefInfo::Ref));
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

Function *feme::cpu::getOrInsertMaskedStore(Module &M, Type *ElementType) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  FunctionType *FTy = FunctionType::get(
      VoidTy, {ElementType, PtrTy, I32Ty, I1Ty}, /*isVarArg=*/false);
  std::string Name =
      mangleMaskedMemOpName("feme.cpu.masked.store.", ElementType);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    F->setMemoryEffects(MemoryEffects::argMemOnly(ModRefInfo::Mod));
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

CallInst *feme::cpu::createMaskedLoad(IRBuilderBase &Builder, Value *Ptr,
                                      unsigned Align, Value *Mask,
                                      Value *Passthru, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertMaskedLoad(*M, Passthru->getType());
  return Builder.CreateCall(
      F, {Ptr, Builder.getInt32(Align), Mask, Passthru}, Name);
}

CallInst *feme::cpu::createMaskedStore(IRBuilderBase &Builder, Value *Val,
                                       Value *Ptr, unsigned Align,
                                       Value *Mask) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertMaskedStore(*M, Val->getType());
  return Builder.CreateCall(F, {Val, Ptr, Builder.getInt32(Align), Mask});
}

std::optional<feme::cpu::MatchedMaskedMemOp>
feme::cpu::matchMaskedLoad(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee || !Callee->getName().starts_with("feme.cpu.masked.load.") ||
      CI.arg_size() != 4)
    return std::nullopt;
  MatchedMaskedMemOp Result;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.Ptr = CI.getArgOperand(0);
  Result.Align =
      static_cast<unsigned>(cast<ConstantInt>(CI.getArgOperand(1))
                                ->getZExtValue());
  Result.Mask = CI.getArgOperand(2);
  Result.ValueOperand = CI.getArgOperand(3);
  return Result;
}

std::optional<feme::cpu::MatchedMaskedMemOp>
feme::cpu::matchMaskedStore(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee || !Callee->getName().starts_with("feme.cpu.masked.store.") ||
      CI.arg_size() != 4)
    return std::nullopt;
  MatchedMaskedMemOp Result;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.ValueOperand = CI.getArgOperand(0);
  Result.Ptr = CI.getArgOperand(1);
  Result.Align =
      static_cast<unsigned>(cast<ConstantInt>(CI.getArgOperand(2))
                                ->getZExtValue());
  Result.Mask = CI.getArgOperand(3);
  return Result;
}
