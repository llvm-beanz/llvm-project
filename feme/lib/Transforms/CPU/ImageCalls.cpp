//===- ImageCalls.cpp - `feme.cpu.image.*` call helpers -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ImageCalls.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

StringRef feme::cpu::getImageCallName(ImageCallKind Kind) {
  switch (Kind) {
  case ImageCallKind::Sample2D:
    return "feme.cpu.image.sample.2d.v4f32";
  case ImageCallKind::SampleCmp2D:
    return "feme.cpu.image.samplecmp.2d.f32";
  case ImageCallKind::Load2D:
    return "feme.cpu.image.load.2d.v4f32";
  }
  llvm_unreachable("unhandled ImageCallKind");
}

Function *feme::cpu::getOrInsertImageCall(Module &M, ImageCallKind Kind) {
  LLVMContext &Ctx = M.getContext();
  Type *PtrTy = PointerType::get(Ctx, 0);
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *F32Ty = Type::getFloatTy(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);
  Type *V4F32Ty = FixedVectorType::get(F32Ty, 4);

  FunctionType *FTy = nullptr;
  switch (Kind) {
  case ImageCallKind::Sample2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, lod, use_explicit_lod, mask)
    // -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I1Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmp2D:
    // Same as Sample2D, plus a trailing `dref` operand; returns `float`.
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I1Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Load2D:
    // (image_heap, image_heap_count, image_index, x, y, mip, mask)
    // -> <4 x float>
    FTy = FunctionType::get(
        V4F32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  }

  StringRef Name = getImageCallName(Kind);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    // Every `feme.cpu.image.*` call only reads through its heap pointer
    // arguments (see `feme::cpu::ResourceCalls::getOrInsertResourceCall`'s
    // identical reasoning for buffers).
    F->setMemoryEffects(MemoryEffects::argMemOnly(ModRefInfo::Ref));
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

CallInst *feme::cpu::createSample2D(IRBuilderBase &Builder,
                                    const ImageCallEnv &Env,
                                    Value *ImageIndex, Value *SamplerIndex,
                                    Value *U, Value *V, Value *Lod,
                                    Value *UseExplicitLod, Value *Mask,
                                    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount,
                             Env.SamplerHeap, Env.SamplerHeapCount, ImageIndex,
                             SamplerIndex, U, V, Lod, UseExplicitLod, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCmp2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *SamplerIndex,
                                       Value *U, Value *V, Value *Lod,
                                       Value *UseExplicitLod, Value *Dref,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmp2D);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
       Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, Lod,
       UseExplicitLod, Dref, Mask},
      Name);
}

CallInst *feme::cpu::createLoad2D(IRBuilderBase &Builder,
                                  const ImageCallEnv &Env, Value *ImageIndex,
                                  Value *X, Value *Y, Value *Mip, Value *Mask,
                                  const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Mip, Mask},
      Name);
}

std::optional<MatchedImageCall> feme::cpu::matchImageCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  static constexpr ImageCallKind AllKinds[] = {
      ImageCallKind::Sample2D, ImageCallKind::SampleCmp2D,
      ImageCallKind::Load2D};

  ImageCallKind Kind;
  bool Found = false;
  for (ImageCallKind K : AllKinds) {
    if (Callee->getName() == getImageCallName(K)) {
      Kind = K;
      Found = true;
      break;
    }
  }
  if (!Found)
    return std::nullopt;

  MatchedImageCall Result;
  Result.Kind = Kind;
  Result.Call = const_cast<CallInst *>(&CI);

  switch (Kind) {
  case ImageCallKind::Sample2D:
    if (CI.arg_size() != 11)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.Lod = CI.getArgOperand(8);
    Result.UseExplicitLod = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::SampleCmp2D:
    if (CI.arg_size() != 12)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.Lod = CI.getArgOperand(8);
    Result.UseExplicitLod = CI.getArgOperand(9);
    Result.Dref = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::Load2D:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Lod = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  }
  return Result;
}
