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

#include <optional>
#include <string>

using namespace llvm;

namespace {

/// Appends the scalar type mangling a `feme.cpu.masked.load`/`.store` name
/// uses, e.g. `f32`, `i32`. Mirrors `feme::cpu::mangleResourceCallName`'s
/// scalar mangling (Transforms/CPU/ResourceCalls.cpp), duplicated here
/// rather than shared: the two families mangle unrelated declarations, and
/// the vector-element case that file's version also handles never arises
/// for a masked load/store operand, which is always the memory access's
/// direct scalar/vector type, not a further-decomposed component.
///
/// Returns false, after reporting \p Ty through its own `LLVMContext`'s
/// diagnostic handler (caught by `feme::cpu::runPipeline`'s
/// `ErrorDiagnosticGuard`, turning it into a graceful pipeline failure
/// rather than a crash), if \p Ty is a shape this milestone does not yet
/// recognize -- a matrix/aggregate element type, most notably. That shape
/// is real, reachable IR (e.g. a masked load/store `feme::cpu::
/// LinearizePass` created for a matrix-typed memory access), not a
/// programmer error, so it must be diagnosed like every other unsupported
/// shape in this codebase (roadmap C8) rather than asserted unreachable.
bool appendScalarMangling(raw_ostream &OS, Type *Ty) {
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
    std::string TyStr;
    raw_string_ostream TyOS(TyStr);
    Ty->print(TyOS);
    Ty->getContext().emitError(
        "feme-cpu-masked-mem-op: unsupported feme.cpu.masked.* element "
        "type '" +
        TyStr +
        "' (matrix/aggregate shapes are not yet legalized for a masked "
        "load/store/atomicrmw -- roadmap C8)");
    return false;
  }
  return true;
}

/// Returns `std::nullopt`, after `appendScalarMangling` has already
/// reported the offending element type, if \p ElementType (or a
/// `FixedVectorType`'s own element type) is unsupported.
std::optional<std::string> mangleMaskedMemOpName(StringRef Prefix,
                                                 Type *ElementType,
                                                 unsigned AddressSpace) {
  std::string Name;
  raw_string_ostream OS(Name);
  OS << Prefix;
  bool Supported;
  if (auto *VecTy = dyn_cast<FixedVectorType>(ElementType)) {
    OS << 'v' << VecTy->getNumElements();
    Supported = appendScalarMangling(OS, VecTy->getElementType());
  } else {
    Supported = appendScalarMangling(OS, ElementType);
  }
  if (!Supported)
    return std::nullopt;
  // Folded into the mangled name, not the declaration's `ptr` parameter
  // type, since an opaque pointer's address space is part of its type --
  // see `feme::cpu::getOrInsertMaskedLoad`'s comment for why two different
  // address spaces sharing one declaration is unsound. Omitted for the
  // overwhelmingly common default address space 0 so every existing
  // mangled name (and every existing test asserting one) is unaffected.
  if (AddressSpace != 0)
    OS << ".as" << AddressSpace;
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

Function *feme::cpu::getOrInsertMaskedLoad(Module &M, Type *ElementType,
                                           unsigned AddressSpace) {
  std::optional<std::string> Name =
      mangleMaskedMemOpName("feme.cpu.masked.load.", ElementType, AddressSpace);
  if (!Name)
    return nullptr;
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, AddressSpace);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  FunctionType *FTy = FunctionType::get(
      ElementType, {PtrTy, I32Ty, I1Ty, ElementType}, /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction(*Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    F->setMemoryEffects(MemoryEffects::argMemOnly(ModRefInfo::Ref));
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

Function *feme::cpu::getOrInsertMaskedStore(Module &M, Type *ElementType,
                                            unsigned AddressSpace) {
  std::optional<std::string> Name = mangleMaskedMemOpName(
      "feme.cpu.masked.store.", ElementType, AddressSpace);
  if (!Name)
    return nullptr;
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, AddressSpace);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  FunctionType *FTy = FunctionType::get(
      VoidTy, {ElementType, PtrTy, I32Ty, I1Ty}, /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction(*Name, FTy).getCallee());
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
  Function *F = getOrInsertMaskedLoad(
      *M, Passthru->getType(),
      cast<PointerType>(Ptr->getType())->getAddressSpace());
  if (!F)
    return nullptr;
  return Builder.CreateCall(F, {Ptr, Builder.getInt32(Align), Mask, Passthru},
                            Name);
}

CallInst *feme::cpu::createMaskedStore(IRBuilderBase &Builder, Value *Val,
                                       Value *Ptr, unsigned Align,
                                       Value *Mask) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertMaskedStore(
      *M, Val->getType(), cast<PointerType>(Ptr->getType())->getAddressSpace());
  if (!F)
    return nullptr;
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
  Result.Align = static_cast<unsigned>(
      cast<ConstantInt>(CI.getArgOperand(1))->getZExtValue());
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
  Result.Align = static_cast<unsigned>(
      cast<ConstantInt>(CI.getArgOperand(2))->getZExtValue());
  Result.Mask = CI.getArgOperand(3);
  return Result;
}

Function *feme::cpu::getOrInsertMaskedAtomicRMW(Module &M, Type *ValueType,
                                                unsigned AddressSpace) {
  std::optional<std::string> Name = mangleMaskedMemOpName(
      "feme.cpu.masked.atomicrmw.", ValueType, AddressSpace);
  if (!Name)
    return nullptr;
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::get(Ctx, AddressSpace);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  FunctionType *FTy = FunctionType::get(
      ValueType, {I32Ty, PtrTy, ValueType, I32Ty, I1Ty}, /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction(*Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    F->setMemoryEffects(MemoryEffects::argMemOnly());
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

CallInst *feme::cpu::createMaskedAtomicRMW(IRBuilderBase &Builder,
                                           AtomicRMWInst::BinOp Op, Value *Ptr,
                                           Value *Val, unsigned Align,
                                           Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertMaskedAtomicRMW(
      *M, Val->getType(), cast<PointerType>(Ptr->getType())->getAddressSpace());
  if (!F)
    return nullptr;
  return Builder.CreateCall(F,
                            {Builder.getInt32(static_cast<uint32_t>(Op)), Ptr,
                             Val, Builder.getInt32(Align), Mask},
                            Name);
}

std::optional<feme::cpu::MatchedMaskedAtomicRMW>
feme::cpu::matchMaskedAtomicRMW(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee || !Callee->getName().starts_with("feme.cpu.masked.atomicrmw.") ||
      CI.arg_size() != 5)
    return std::nullopt;
  MatchedMaskedAtomicRMW Result;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.Op = static_cast<AtomicRMWInst::BinOp>(
      cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue());
  Result.Ptr = CI.getArgOperand(1);
  Result.Val = CI.getArgOperand(2);
  Result.Align = static_cast<unsigned>(
      cast<ConstantInt>(CI.getArgOperand(3))->getZExtValue());
  Result.Mask = CI.getArgOperand(4);
  return Result;
}
