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
  ShaderResourcesFieldImageHeap = 2,
  ShaderResourcesFieldImageHeapCount = 3,
  ShaderResourcesFieldSamplerHeap = 4,
  ShaderResourcesFieldSamplerHeapCount = 5,
  ShaderResourcesFieldRootConstants = 6,
  ShaderResourcesFieldRootConstantSize = 7,
  ShaderResourcesFieldSubpassInputHeap = 8,
  ShaderResourcesFieldSubpassInputHeapCount = 9,
  ShaderResourcesFieldReserved = 10,
};

enum VertexInvocationField : unsigned {
  VertexInvocationFieldVertexID = 0,
  VertexInvocationFieldInstanceID = 1,
  VertexInvocationFieldBaseVertex = 2,
  VertexInvocationFieldBaseInstance = 3,
  VertexInvocationFieldDrawID = 4,
  VertexInvocationFieldViewIndex = 5,
  VertexInvocationFieldReserved = 6,
};

enum FragmentInvocationField : unsigned {
  FragmentInvocationFieldPosition = 0,
  FragmentInvocationFieldPrimitiveID = 1,
  FragmentInvocationFieldSampleIndex = 2,
  FragmentInvocationFieldCoverage = 3,
  FragmentInvocationFieldIsFrontFace = 4,
  FragmentInvocationFieldViewportIndex = 5,
  FragmentInvocationFieldViewIndex = 6,
  FragmentInvocationFieldLiveMask = 7,
  FragmentInvocationFieldSideEffectMask = 8,
  FragmentInvocationFieldReserved = 9,
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

enum PatchArgsField : unsigned {
  PatchArgsFieldAbiVersion = 0,
  PatchArgsFieldOutputControlPointCount = 1,
  PatchArgsFieldInputPatchControlPointCount = 2,
  PatchArgsFieldReserved32 = 3,
  PatchArgsFieldResources = 4,
  PatchArgsFieldInputLayout = 5,
  PatchArgsFieldInputs = 6,
  PatchArgsFieldOutputLayout = 7,
  PatchArgsFieldOutputs = 8,
  PatchArgsFieldReserved = 9,
};

enum PatchConstantArgsField : unsigned {
  PatchConstantArgsFieldAbiVersion = 0,
  PatchConstantArgsFieldOutputControlPointCount = 1,
  PatchConstantArgsFieldInputPatchControlPointCount = 2,
  PatchConstantArgsFieldReserved32 = 3,
  PatchConstantArgsFieldResources = 4,
  PatchConstantArgsFieldInputLayout = 5,
  PatchConstantArgsFieldInputs = 6,
  PatchConstantArgsFieldInputPatchLayout = 7,
  PatchConstantArgsFieldInputPatch = 8,
  PatchConstantArgsFieldOutputLayout = 9,
  PatchConstantArgsFieldOutputs = 10,
  PatchConstantArgsFieldReserved = 11,
};

enum DomainInvocationField : unsigned {
  DomainInvocationFieldDomainLocation = 0,
  DomainInvocationFieldReserved = 1,
};

enum DomainArgsField : unsigned {
  DomainArgsFieldAbiVersion = 0,
  DomainArgsFieldDomainPointCount = 1,
  DomainArgsFieldOutputControlPointCount = 2,
  DomainArgsFieldReserved32 = 3,
  DomainArgsFieldResources = 4,
  DomainArgsFieldInputLayout = 5,
  DomainArgsFieldInputs = 6,
  DomainArgsFieldPatchConstantLayout = 7,
  DomainArgsFieldPatchConstants = 8,
  DomainArgsFieldOutputLayout = 9,
  DomainArgsFieldOutputs = 10,
  DomainArgsFieldInvocations = 11,
  DomainArgsFieldReserved = 12,
};

enum GeometryInvocationField : unsigned {
  GeometryInvocationFieldPrimitiveID = 0,
  GeometryInvocationFieldReserved = 1,
};

enum GeometryArgsField : unsigned {
  GeometryArgsFieldAbiVersion = 0,
  GeometryArgsFieldPrimitiveCount = 1,
  GeometryArgsFieldVerticesPerPrimitive = 2,
  GeometryArgsFieldMaxVerticesPerStream = 3,
  GeometryArgsFieldOutputScalarsPerVertex = 4,
  GeometryArgsFieldReserved32 = 5,
  GeometryArgsFieldResources = 6,
  GeometryArgsFieldInputLayout = 7,
  GeometryArgsFieldInputs = 8,
  GeometryArgsFieldOutputLayout = 9,
  GeometryArgsFieldOutputs = 10,
  GeometryArgsFieldInvocations = 11,
  GeometryArgsFieldEmittedVertices = 12,
  GeometryArgsFieldEmittedVertexCounts = 13,
  GeometryArgsFieldStripEndsAfter = 14,
  GeometryArgsFieldReserved = 15,
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
                                     PtrTy, I32Ty, PtrTy, I32Ty,
                                     llvm::ArrayType::get(PtrTy, 2)});
}

inline llvm::StructType *getVertexInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty,
                                     llvm::ArrayType::get(I32Ty, 2)});
}

inline llvm::StructType *getFragmentInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *F32Ty = llvm::Type::getFloatTy(Ctx);
  llvm::Type *PositionTy =
      llvm::ArrayType::get(llvm::ArrayType::get(F32Ty, 4), 4);
  llvm::Type *I32x4 = llvm::ArrayType::get(I32Ty, 4);
  return llvm::StructType::get(Ctx, {PositionTy, I32x4, I32x4, I32x4, I32x4,
                                     I32x4, I32Ty, I32Ty, I32Ty,
                                     llvm::ArrayType::get(I32Ty, 4)});
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

inline llvm::StructType *getPatchArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx,
                               {I32Ty, I32Ty, I32Ty, I32Ty, PtrTy, PtrTy, PtrTy,
                                PtrTy, PtrTy, llvm::ArrayType::get(PtrTy, 4)});
}

/// Distinct from `getPatchArgsType` (see `FemePatchConstantArgs`'s own
/// comment for why): this phase's ABI carries a second, independent
/// structure-of-arrays input block (`InputPatch`/`InputPatchLayout`) for the
/// original, pre-control-stage input control points, alongside the
/// completed `OutputPatch` every patch-constant invocation reads.
inline llvm::StructType *getPatchConstantArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32Ty, I32Ty, I32Ty, I32Ty, PtrTy, PtrTy,
                                     PtrTy, PtrTy, PtrTy, PtrTy, PtrTy,
                                     llvm::ArrayType::get(PtrTy, 2)});
}

/// Mirrors `FemeDomainInvocation`: the tessellator-generated domain
/// coordinate one domain-stage invocation evaluates.
inline llvm::StructType *getDomainInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *F32Ty = llvm::Type::getFloatTy(Ctx);
  return llvm::StructType::get(
      Ctx, {llvm::ArrayType::get(F32Ty, 3), llvm::ArrayType::get(I32Ty, 5)});
}

/// Mirrors `FemeDomainArgs`: a vertex-shaped per-invocation batch whose
/// inputs additionally carry the completed patch's control points and the
/// per-patch tessellation factors/patch constants.
inline llvm::StructType *getDomainArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32Ty, I32Ty, I32Ty, I32Ty, PtrTy, PtrTy,
                                     PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy,
                                     llvm::ArrayType::get(PtrTy, 2)});
}

/// Mirrors `FemeGeometryInvocation`: the input primitive one geometry-stage
/// invocation processes.
inline llvm::StructType *getGeometryInvocationType(llvm::LLVMContext &Ctx) {
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32Ty, llvm::ArrayType::get(I32Ty, 7)});
}

/// Mirrors `FemeGeometryArgs`: a vertex-shaped per-invocation batch whose
/// inputs are structure-of-arrays over assembled primitives (not single
/// vertices) and whose output is bounded, multi-vertex `emit`/`cut` stream
/// storage rather than one fixed per-invocation result slot.
inline llvm::StructType *getGeometryArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  return llvm::StructType::get(Ctx,
                               {I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, PtrTy,
                                PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy, PtrTy,
                                PtrTy, llvm::ArrayType::get(PtrTy, 2)});
}

inline llvm::Value *loadStructField(llvm::IRBuilder<> &Builder,
                                    llvm::StructType *Ty, llvm::Value *Ptr,
                                    unsigned Field, llvm::Type *FieldTy) {
  llvm::Value *FieldPtr = Builder.CreateStructGEP(Ty, Ptr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_STAGEARGSLAYOUT_H
