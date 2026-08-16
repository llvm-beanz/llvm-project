//===- StageMaskCalls.cpp - CPU-internal masked stage side effects --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StageMaskCalls.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

void appendTypeSuffix(SmallVectorImpl<char> &Out, Type *Ty) {
  raw_svector_ostream OS(Out);
  Type *Scalar = Ty;
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    OS << 'v' << VecTy->getNumElements();
    Scalar = VecTy->getElementType();
  }
  if (Scalar->isFloatTy())
    OS << "f32";
  else if (Scalar->isDoubleTy())
    OS << "f64";
  else if (Scalar->isHalfTy())
    OS << "f16";
  else if (auto *IntTy = dyn_cast<IntegerType>(Scalar))
    OS << 'i' << IntTy->getBitWidth();
  else
    OS << "unknown";
}

} // namespace

FunctionCallee feme::cpu::getOrInsertMaskedOutputStore(Module &M, Type *ValueTy,
                                                       Type *RowTy,
                                                       Type *ComponentTy,
                                                       Type *VertexTy,
                                                       Type *MaskTy) {
  SmallString<64> Name(MaskedOutputStorePrefix);
  Name.push_back('.');
  appendTypeSuffix(Name, ValueTy);
  FunctionType *FTy =
      FunctionType::get(Type::getVoidTy(M.getContext()),
                        {Type::getInt32Ty(M.getContext()), RowTy, ComponentTy,
                         ValueTy, VertexTy, MaskTy},
                        /*isVarArg=*/false);
  return M.getOrInsertFunction(Name, FTy);
}

CallInst *feme::cpu::createMaskedOutputStore(IRBuilderBase &B,
                                             uint32_t ElementID, Value *Row,
                                             Value *Component, Value *ValueArg,
                                             Value *Vertex, Value *Mask) {
  Module *M = B.GetInsertBlock()->getModule();
  Value *Element = ConstantInt::get(B.getInt32Ty(), ElementID);
  FunctionCallee Callee = getOrInsertMaskedOutputStore(
      *M, ValueArg->getType(), Row->getType(), Component->getType(),
      Vertex->getType(), Mask->getType());
  return B.CreateCall(Callee,
                      {Element, Row, Component, ValueArg, Vertex, Mask});
}

FunctionCallee feme::cpu::getOrInsertReturnMasks(Module &M, Type *LiveTy,
                                                 Type *SideEffectTy) {
  SmallString<64> Name(ReturnMasksPrefix);
  Name.push_back('.');
  appendTypeSuffix(Name, LiveTy);
  FunctionType *FTy =
      FunctionType::get(Type::getVoidTy(M.getContext()), {LiveTy, SideEffectTy},
                        /*isVarArg=*/false);
  return M.getOrInsertFunction(Name, FTy);
}

CallInst *feme::cpu::createReturnMasks(IRBuilderBase &B, Value *Live,
                                       Value *SideEffect) {
  Module *M = B.GetInsertBlock()->getModule();
  FunctionCallee Callee =
      getOrInsertReturnMasks(*M, Live->getType(), SideEffect->getType());
  return B.CreateCall(Callee, {Live, SideEffect});
}

bool feme::cpu::isMaskedOutputStoreCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  return Callee && Callee->getName().starts_with(MaskedOutputStorePrefix);
}

bool feme::cpu::isReturnMasksCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  return Callee && Callee->getName().starts_with(ReturnMasksPrefix);
}
