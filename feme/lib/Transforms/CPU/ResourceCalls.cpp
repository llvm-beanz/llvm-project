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
  // offset) operands; loads return `ElementType` and stores instead take it
  // as a trailing value operand ahead of the mask (see "Lowering").
  SmallVector<Type *, 6> Params = {PtrTy, I32Ty, I32Ty, I64Ty};
  Type *RetTy = Type::getVoidTy(Ctx);
  if (isLoad(Kind)) {
    RetTy = ElementType;
  } else {
    Params.push_back(ElementType);
  }
  Params.push_back(I1Ty);

  std::string Name = mangleResourceCallName(Kind, ElementType);
  FunctionType *FTy = FunctionType::get(RetTy, Params, /*isVarArg=*/false);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());

  // These are ordinary declarations with attributes describing their memory
  // effects (see "Lowering"): a load only reads through the heap pointer
  // argument, a store only writes through it, and neither has any other
  // observable side effect, so the ordinary optimizer can reason about them
  // (CSE a repeated load, sink/hoist across unrelated code, ...) once the
  // helper implementation is linked in and inlined.
  if (!F->hasFnAttribute(Attribute::Memory)) {
    MemoryEffects ME = isLoad(Kind)
                           ? MemoryEffects::argMemOnly(ModRefInfo::Ref)
                           : MemoryEffects::argMemOnly(ModRefInfo::Mod);
    F->setMemoryEffects(ME);
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

static CallInst *createCall(IRBuilderBase &Builder, ResourceCallKind Kind,
                            const ResourceCallEnv &Env, Value *DescriptorIndex,
                            Value *Offset, Value *StoredValue, Value *Mask,
                            Type *ElementType, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertResourceCall(*M, Kind, ElementType);
  SmallVector<Value *, 6> Args = {Env.ResourceHeap, Env.ResourceHeapCount,
                                  DescriptorIndex, Offset};
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
                    ElementIndex, /*StoredValue=*/nullptr, Mask, ElementType,
                    Name);
}

CallInst *feme::cpu::createTypedStore(IRBuilderBase &Builder,
                                      const ResourceCallEnv &Env,
                                      Value *DescriptorIndex,
                                      Value *ElementIndex, Value *StoredValue,
                                      Value *Mask) {
  return createCall(Builder, ResourceCallKind::StoreTyped, Env, DescriptorIndex,
                    ElementIndex, StoredValue, Mask, StoredValue->getType(),
                    "");
}

CallInst *feme::cpu::createRawLoad(IRBuilderBase &Builder,
                                   const ResourceCallEnv &Env,
                                   Value *DescriptorIndex, Value *ByteOffset,
                                   Value *Mask, Type *ElementType,
                                   const Twine &Name) {
  return createCall(Builder, ResourceCallKind::LoadRaw, Env, DescriptorIndex,
                    ByteOffset, /*StoredValue=*/nullptr, Mask, ElementType,
                    Name);
}

CallInst *feme::cpu::createRawStore(IRBuilderBase &Builder,
                                    const ResourceCallEnv &Env,
                                    Value *DescriptorIndex, Value *ByteOffset,
                                    Value *StoredValue, Value *Mask) {
  return createCall(Builder, ResourceCallKind::StoreRaw, Env, DescriptorIndex,
                    ByteOffset, StoredValue, Mask, StoredValue->getType(), "");
}

std::optional<MatchedResourceCall>
feme::cpu::matchResourceCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  static constexpr ResourceCallKind AllKinds[] = {
      ResourceCallKind::LoadTyped, ResourceCallKind::StoreTyped,
      ResourceCallKind::LoadRaw, ResourceCallKind::StoreRaw};

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
  // unrelated call that merely starts with the same prefix).
  unsigned ExpectedArgs = isLoad(Kind) ? 5 : 6;
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
  } else {
    Result.StoredValue = CI.getArgOperand(4);
    Result.Mask = CI.getArgOperand(5);
    Result.ElementType = Result.StoredValue->getType();
  }
  return Result;
}
