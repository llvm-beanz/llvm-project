//===- ResourceCalls.cpp - `feme.cpu.resource.*` call helpers ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ResourceCalls.h"

#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The name prefix each `ResourceCallKind` mangles onto, before the element
/// type suffix (see `mangleResourceCallName`).
StringRef getNamePrefix(ResourceCallKind Kind) {
  switch (Kind) {
  case ResourceCallKind::LoadTyped:
    return "feme.cpu.resource.load.typed.";
  case ResourceCallKind::StoreTyped:
    return "feme.cpu.resource.store.typed.";
  case ResourceCallKind::LoadRaw:
    return "feme.cpu.resource.load.raw.";
  case ResourceCallKind::StoreRaw:
    return "feme.cpu.resource.store.raw.";
  case ResourceCallKind::AtomicAddTyped:
    return "feme.cpu.resource.atomic.add.typed.";
  case ResourceCallKind::AtomicSubTyped:
    return "feme.cpu.resource.atomic.sub.typed.";
  case ResourceCallKind::AtomicAndTyped:
    return "feme.cpu.resource.atomic.and.typed.";
  case ResourceCallKind::AtomicOrTyped:
    return "feme.cpu.resource.atomic.or.typed.";
  case ResourceCallKind::AtomicXorTyped:
    return "feme.cpu.resource.atomic.xor.typed.";
  case ResourceCallKind::AtomicSMaxTyped:
    return "feme.cpu.resource.atomic.smax.typed.";
  case ResourceCallKind::AtomicSMinTyped:
    return "feme.cpu.resource.atomic.smin.typed.";
  case ResourceCallKind::AtomicUMaxTyped:
    return "feme.cpu.resource.atomic.umax.typed.";
  case ResourceCallKind::AtomicUMinTyped:
    return "feme.cpu.resource.atomic.umin.typed.";
  case ResourceCallKind::AtomicExchangeTyped:
    return "feme.cpu.resource.atomic.exchange.typed.";
  case ResourceCallKind::AtomicCompareExchangeTyped:
    return "feme.cpu.resource.atomic.compare_exchange.typed.";
  }
  llvm_unreachable("unhandled ResourceCallKind");
}

/// Appends the scalar type mangling `mangleResourceCallName` uses, e.g.
/// `f32`, `i32`, `i1`. Only the scalar/vector element kinds the canonical
/// resource calls support are handled (see "Descriptor formats" in
/// feme/docs/FeMeCPUDesign.md); anything else is a programming error in the
/// caller, not a malformed-input case to recover from.
void appendScalarMangling(raw_ostream &OS, Type *Ty) {
  if (Ty->isHalfTy()) {
    OS << "f16";
  } else if (Ty->isFloatTy()) {
    OS << "f32";
  } else if (Ty->isDoubleTy()) {
    OS << "f64";
  } else if (Ty->isIntegerTy()) {
    OS << "i" << Ty->getIntegerBitWidth();
  } else {
    llvm_unreachable("unsupported feme.cpu.resource.* element type");
  }
}

} // namespace

bool feme::cpu::isLoad(ResourceCallKind Kind) {
  return Kind == ResourceCallKind::LoadTyped ||
         Kind == ResourceCallKind::LoadRaw;
}

bool feme::cpu::isAtomic(ResourceCallKind Kind) {
  switch (Kind) {
  case ResourceCallKind::AtomicAddTyped:
  case ResourceCallKind::AtomicSubTyped:
  case ResourceCallKind::AtomicAndTyped:
  case ResourceCallKind::AtomicOrTyped:
  case ResourceCallKind::AtomicXorTyped:
  case ResourceCallKind::AtomicSMaxTyped:
  case ResourceCallKind::AtomicSMinTyped:
  case ResourceCallKind::AtomicUMaxTyped:
  case ResourceCallKind::AtomicUMinTyped:
  case ResourceCallKind::AtomicExchangeTyped:
  case ResourceCallKind::AtomicCompareExchangeTyped:
    return true;
  case ResourceCallKind::LoadTyped:
  case ResourceCallKind::StoreTyped:
  case ResourceCallKind::LoadRaw:
  case ResourceCallKind::StoreRaw:
    return false;
  }
  llvm_unreachable("unhandled ResourceCallKind");
}

std::string feme::cpu::mangleResourceCallName(ResourceCallKind Kind,
                                              Type *ElementType) {
  std::string Name;
  raw_string_ostream OS(Name);
  OS << getNamePrefix(Kind);
  if (auto *VecTy = dyn_cast<FixedVectorType>(ElementType)) {
    OS << 'v' << VecTy->getNumElements();
    appendScalarMangling(OS, VecTy->getElementType());
  } else {
    appendScalarMangling(OS, ElementType);
  }
  return Name;
}

Function *feme::cpu::getOrInsertResourceCall(Module &M, ResourceCallKind Kind,
                                             Type *ElementType) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);

  // Every call shares the leading (heap, heap_count, descriptor_index,
  // offset) operands; loads return `ElementType` with no trailing value
  // operand, an ordinary store instead takes it as a trailing value operand
  // ahead of the mask (see "Lowering"), and an atomic (roadmap H8w) takes
  // it too but *also* returns `ElementType` (the pre-op value) --
  // `AtomicCompareExchangeTyped` alone takes a second, leading comparator
  // operand ahead of the value.
  SmallVector<Type *, 6> Params = {PtrTy, I32Ty, I32Ty, I64Ty};
  Type *RetTy = Type::getVoidTy(Ctx);
  if (isLoad(Kind)) {
    RetTy = ElementType;
  } else {
    if (Kind == ResourceCallKind::AtomicCompareExchangeTyped)
      Params.push_back(ElementType);
    Params.push_back(ElementType);
    if (isAtomic(Kind))
      RetTy = ElementType;
  }
  Params.push_back(I1Ty);

  std::string Name = mangleResourceCallName(Kind, ElementType);
  FunctionType *FTy = FunctionType::get(RetTy, Params, /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());

  // These are ordinary declarations with attributes describing their memory
  // effects (see "Lowering"): a load only reads through the heap pointer
  // argument, an ordinary store only writes through it, an atomic
  // (roadmap H8w) does both in one call (it reads the pre-op value *and*
  // writes the new one), and neither has any other observable side effect,
  // so the ordinary optimizer can reason about them (CSE a repeated load,
  // sink/hoist across unrelated code, ...) once the helper implementation
  // is linked in and inlined. An atomic still cannot be reordered with
  // another memory access the way a load/store pair can (`ModRefInfo::ModRef`
  // rather than a one-sided `Ref`/`Mod`), matching how the optimizer already
  // treats `AtomicRMWInst`/`AtomicCmpXchgInst` themselves.
  if (!F->hasFnAttribute(Attribute::Memory)) {
    MemoryEffects ME = MemoryEffects::argMemOnly(ModRefInfo::Mod);
    if (isLoad(Kind))
      ME = MemoryEffects::argMemOnly(ModRefInfo::Ref);
    else if (isAtomic(Kind))
      ME = MemoryEffects::argMemOnly(ModRefInfo::ModRef);
    F->setMemoryEffects(ME);
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

static CallInst *createCall(IRBuilderBase &Builder, ResourceCallKind Kind,
                            const ResourceCallEnv &Env, Value *DescriptorIndex,
                            Value *Offset, Value *Comparator,
                            Value *StoredValue, Value *Mask,
                            Type *ElementType, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertResourceCall(*M, Kind, ElementType);
  SmallVector<Value *, 7> Args = {Env.ResourceHeap, Env.ResourceHeapCount,
                                  DescriptorIndex, Offset};
  if (Kind == ResourceCallKind::AtomicCompareExchangeTyped)
    Args.push_back(Comparator);
  if (!isLoad(Kind))
    Args.push_back(StoredValue);
  Args.push_back(Mask);
  return Builder.CreateCall(F, Args, Name);
}

CallInst *feme::cpu::createTypedLoad(IRBuilderBase &Builder,
                                     const ResourceCallEnv &Env,
                                     Value *DescriptorIndex,
                                     Value *ElementIndex, Value *Mask,
                                     Type *ElementType, const Twine &Name) {
  return createCall(Builder, ResourceCallKind::LoadTyped, Env, DescriptorIndex,
                    ElementIndex, /*Comparator=*/nullptr,
                    /*StoredValue=*/nullptr, Mask, ElementType, Name);
}

CallInst *feme::cpu::createTypedStore(IRBuilderBase &Builder,
                                      const ResourceCallEnv &Env,
                                      Value *DescriptorIndex,
                                      Value *ElementIndex, Value *StoredValue,
                                      Value *Mask) {
  return createCall(Builder, ResourceCallKind::StoreTyped, Env, DescriptorIndex,
                    ElementIndex, /*Comparator=*/nullptr, StoredValue, Mask,
                    StoredValue->getType(), "");
}

CallInst *feme::cpu::createRawLoad(IRBuilderBase &Builder,
                                   const ResourceCallEnv &Env,
                                   Value *DescriptorIndex, Value *ByteOffset,
                                   Value *Mask, Type *ElementType,
                                   const Twine &Name) {
  return createCall(Builder, ResourceCallKind::LoadRaw, Env, DescriptorIndex,
                    ByteOffset, /*Comparator=*/nullptr,
                    /*StoredValue=*/nullptr, Mask, ElementType, Name);
}

CallInst *feme::cpu::createRawStore(IRBuilderBase &Builder,
                                    const ResourceCallEnv &Env,
                                    Value *DescriptorIndex, Value *ByteOffset,
                                    Value *StoredValue, Value *Mask) {
  return createCall(Builder, ResourceCallKind::StoreRaw, Env, DescriptorIndex,
                    ByteOffset, /*Comparator=*/nullptr, StoredValue, Mask,
                    StoredValue->getType(), "");
}

/// Shared body for every `createAtomic*Typed` builder (roadmap H8w): they
/// differ only in `Kind`.
static CallInst *createAtomicTyped(IRBuilderBase &Builder,
                                   ResourceCallKind Kind,
                                   const ResourceCallEnv &Env,
                                   Value *DescriptorIndex,
                                   Value *ElementIndex, Value *Val,
                                   Value *Mask, const Twine &Name) {
  return createCall(Builder, Kind, Env, DescriptorIndex, ElementIndex,
                    /*Comparator=*/nullptr, Val, Mask, Val->getType(),
                    Name);
}

CallInst *feme::cpu::createAtomicAddTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicAddTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicSubTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicSubTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicAndTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicAndTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicOrTyped(IRBuilderBase &Builder,
                                        const ResourceCallEnv &Env,
                                        Value *DescriptorIndex,
                                        Value *ElementIndex, Value *Val,
                                        Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicOrTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicXorTyped(IRBuilderBase &Builder,
                                         const ResourceCallEnv &Env,
                                         Value *DescriptorIndex,
                                         Value *ElementIndex, Value *Val,
                                         Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicXorTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicSMaxTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicSMaxTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicSMinTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicSMinTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicUMaxTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicUMaxTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicUMinTyped(IRBuilderBase &Builder,
                                          const ResourceCallEnv &Env,
                                          Value *DescriptorIndex,
                                          Value *ElementIndex, Value *Val,
                                          Value *Mask, const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicUMinTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicExchangeTyped(IRBuilderBase &Builder,
                                               const ResourceCallEnv &Env,
                                               Value *DescriptorIndex,
                                               Value *ElementIndex,
                                               Value *Val, Value *Mask,
                                               const Twine &Name) {
  return createAtomicTyped(Builder, ResourceCallKind::AtomicExchangeTyped, Env,
                           DescriptorIndex, ElementIndex, Val, Mask, Name);
}

CallInst *feme::cpu::createAtomicCompareExchangeTyped(
    IRBuilderBase &Builder, const ResourceCallEnv &Env,
    Value *DescriptorIndex, Value *ElementIndex, Value *Comparator,
    Value *Val, Value *Mask, const Twine &Name) {
  return createCall(Builder, ResourceCallKind::AtomicCompareExchangeTyped, Env,
                    DescriptorIndex, ElementIndex, Comparator, Val, Mask,
                    Val->getType(), Name);
}

std::optional<MatchedResourceCall>
feme::cpu::matchResourceCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  static constexpr ResourceCallKind AllKinds[] = {
      ResourceCallKind::LoadTyped,
      ResourceCallKind::StoreTyped,
      ResourceCallKind::LoadRaw,
      ResourceCallKind::StoreRaw,
      ResourceCallKind::AtomicAddTyped,
      ResourceCallKind::AtomicSubTyped,
      ResourceCallKind::AtomicAndTyped,
      ResourceCallKind::AtomicOrTyped,
      ResourceCallKind::AtomicXorTyped,
      ResourceCallKind::AtomicSMaxTyped,
      ResourceCallKind::AtomicSMinTyped,
      ResourceCallKind::AtomicUMaxTyped,
      ResourceCallKind::AtomicUMinTyped,
      ResourceCallKind::AtomicExchangeTyped,
      ResourceCallKind::AtomicCompareExchangeTyped,
  };

  ResourceCallKind Kind;
  bool Found = false;
  for (ResourceCallKind K : AllKinds) {
    if (Callee->getName().starts_with(getNamePrefix(K))) {
      Kind = K;
      Found = true;
      break;
    }
  }
  if (!Found)
    return std::nullopt;

  // A same-named-but-different-shape function is not one this module
  // produced; matching the operand count guards against that (e.g. some
  // unrelated call that merely starts with the same prefix). A load takes
  // no value operand, an ordinary store or simple atomic (roadmap H8w)
  // takes one, and `AtomicCompareExchangeTyped` alone takes two (comparator
  // then value).
  unsigned ExpectedArgs = 6;
  if (isLoad(Kind))
    ExpectedArgs = 5;
  else if (Kind == ResourceCallKind::AtomicCompareExchangeTyped)
    ExpectedArgs = 7;
  if (CI.arg_size() != ExpectedArgs)
    return std::nullopt;

  MatchedResourceCall Result;
  Result.Kind = Kind;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.Env.ResourceHeap = CI.getArgOperand(0);
  Result.Env.ResourceHeapCount = CI.getArgOperand(1);
  Result.DescriptorIndex = CI.getArgOperand(2);
  Result.Offset = CI.getArgOperand(3);
  if (isLoad(Kind)) {
    Result.Mask = CI.getArgOperand(4);
    Result.ElementType = CI.getType();
  } else if (Kind == ResourceCallKind::AtomicCompareExchangeTyped) {
    Result.Comparator = CI.getArgOperand(4);
    Result.StoredValue = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    Result.ElementType = Result.StoredValue->getType();
  } else {
    Result.StoredValue = CI.getArgOperand(4);
    Result.Mask = CI.getArgOperand(5);
    Result.ElementType = Result.StoredValue->getType();
  }
  return Result;
}
