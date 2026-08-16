//===- StageArgsLayout.h - Graphics stage ABI LLVM struct layouts -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Private to feme/lib/Transforms/CPU: LLVM struct types mirroring the graphics
// stage ABI structs in feme/include/feme/Target/CPU/RuntimeABI.h, shared by
// the stage wrapper passes.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_CPU_STAGEARGSLAYOUT_H
#define FEME_LIB_TRANSFORMS_CPU_STAGEARGSLAYOUT_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

namespace feme::cpu {

enum StageElementField : unsigned {
  StageElementFieldElementID = 0,
  StageElementFieldScalarKind = 1,
  StageElementFieldBitWidth = 2,
  StageElementFieldFirstComponent = 3,
  StageElementFieldComponentCount = 4,
  StageElementFieldRowCount = 5,
  StageElementFieldInterpolation = 6,
  StageElementFieldFrequency = 7,
  StageElementFieldSystemValue = 8,
  StageElementFieldInvocationStride = 9,
  StageElementFieldComponentStride = 10,
  StageElementFieldRowStride = 11,
  StageElementFieldDataOffset = 12,
  StageElementFieldFlags = 13,
  StageElementFieldReserved = 14,
};

enum StageLayoutField : unsigned {
  StageLayoutFieldElements = 0,
  StageLayoutFieldElementCount = 1,
  StageLayoutFieldReserved = 2,
};

enum ShaderResourcesField : unsigned {
  ShaderResourcesFieldResourceHeap = 0,
  ShaderResourcesFieldResourceHeapCount = 1,
  ShaderResourcesFieldSamplerHeap = 2,
  ShaderResourcesFieldSamplerHeapCount = 3,
  ShaderResourcesFieldRootConstants = 4,
  ShaderResourcesFieldRootConstantSize = 5,
  ShaderResourcesFieldReserved = 6,
};

enum VertexInvocationField : unsigned {
  VertexInvocationFieldVertexID = 0,
  VertexInvocationFieldInstanceID = 1,
  VertexInvocationFieldBaseVertex = 2,
  VertexInvocationFieldBaseInstance = 3,
  VertexInvocationFieldDrawID = 4,
  VertexInvocationFieldReserved = 5,
};

enum FragmentInvocationField : unsigned {
  FragmentInvocationFieldPosition = 0,
  FragmentInvocationFieldPrimitiveID = 1,
  FragmentInvocationFieldSampleIndex = 2,
  FragmentInvocationFieldCoverage = 3,
  FragmentInvocationFieldIsFrontFace = 4,
  FragmentInvocationFieldLiveMask = 5,
  FragmentInvocationFieldSideEffectMask = 6,
  FragmentInvocationFieldReserved = 7,
};

enum FragmentResultField : unsigned {
  FragmentResultFieldLiveMask = 0,
  FragmentResultFieldSideEffectMask = 1,
  FragmentResultFieldReserved = 2,
};

enum VertexArgsField : unsigned {
  VertexArgsFieldAbiVersion = 0,
  VertexArgsFieldInvocationCount = 1,
  VertexArgsFieldReserved32 = 2,
  VertexArgsFieldResources = 3,
  VertexArgsFieldInputLayout = 4,
  VertexArgsFieldInputs = 5,
  VertexArgsFieldOutputLayout = 6,
  VertexArgsFieldOutputs = 7,
  VertexArgsFieldInvocations = 8,
  VertexArgsFieldReserved = 9,
};

enum FragmentArgsField : unsigned {
  FragmentArgsFieldAbiVersion = 0,
  FragmentArgsFieldQuadCount = 1,
  FragmentArgsFieldReserved32 = 2,
  FragmentArgsFieldResources = 3,
  FragmentArgsFieldInputLayout = 4,
  FragmentArgsFieldInputs = 5,
  FragmentArgsFieldOutputLayout = 6,
  FragmentArgsFieldOutputs = 7,
  FragmentArgsFieldInvocations = 8,
  FragmentArgsFieldResults = 9,
  FragmentArgsFieldReserved = 10,
};

inline llvm::StructType *getStageElementType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *I64Ty = llvm::Type::getInt64Ty(Ctx);
  return llvm::StructType::get(
      Ctx, {I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty,
            I32Ty, I32Ty, I32Ty, I64Ty, I32Ty, llvm::ArrayType::get(I32Ty, 3)});
}

inline llvm::StructType *getStageLayoutType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx,
                               {PtrTy, I32Ty, llvm::ArrayType::get(I32Ty, 7)});
}

inline llvm::StructType *getShaderResourcesType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx, {PtrTy, I32Ty, PtrTy, I32Ty, PtrTy, I32Ty,
                                     llvm::ArrayType::get(PtrTy, 4)});
}

inline llvm::StructType *getVertexInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(
      Ctx, {I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, llvm::ArrayType::get(I32Ty, 3)});
}

inline llvm::StructType *getFragmentInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *F32Ty = llvm::Type::getFloatTy(Ctx);
  llvm::Type *PositionTy =
      llvm::ArrayType::get(llvm::ArrayType::get(F32Ty, 4), 4);
  llvm::Type *I32x4 = llvm::ArrayType::get(I32Ty, 4);
  return llvm::StructType::get(Ctx,
                               {PositionTy, I32x4, I32x4, I32x4, I32x4, I32Ty,
                                I32Ty, llvm::ArrayType::get(I32Ty, 6)});
}

inline llvm::StructType *getFragmentResultType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx,
                               {I32Ty, I32Ty, llvm::ArrayType::get(I32Ty, 6)});
}

inline llvm::StructType *getVertexArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(
      Ctx, {I32Ty, I32Ty, llvm::ArrayType::get(I32Ty, 2), PtrTy, PtrTy, PtrTy,
            PtrTy, PtrTy, PtrTy, llvm::ArrayType::get(PtrTy, 4)});
}

inline llvm::StructType *getFragmentArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(
      Ctx, {I32Ty, I32Ty, llvm::ArrayType::get(I32Ty, 2), PtrTy, PtrTy, PtrTy,
            PtrTy, PtrTy, PtrTy, PtrTy, llvm::ArrayType::get(PtrTy, 4)});
}

inline llvm::Value *loadStructField(llvm::IRBuilder<> &Builder,
                                    llvm::StructType *Ty, llvm::Value *Ptr,
                                    unsigned Field, llvm::Type *FieldTy) {
  llvm::Value *FieldPtr = Builder.CreateStructGEP(Ty, Ptr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_STAGEARGSLAYOUT_H
