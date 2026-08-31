//===- ImageCalls.cpp - `feme.cpu.image.*` call helpers -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/ImageCalls.h"

#include "feme/Core/ShaderStage.h"
#include "feme/Core/StageOps.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
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
  case ImageCallKind::Load2DI32:
    return "feme.cpu.image.load.2d.v4i32";
  case ImageCallKind::Sample2DArray:
    return "feme.cpu.image.sample.2darray.v4f32";
  case ImageCallKind::Load2DArray:
    return "feme.cpu.image.load.2darray.v4f32";
  case ImageCallKind::Load2DArrayI32:
    return "feme.cpu.image.load.2darray.v4i32";
  case ImageCallKind::SampleCube:
    return "feme.cpu.image.sample.cube.v4f32";
  case ImageCallKind::SampleCubeArray:
    return "feme.cpu.image.sample.cubearray.v4f32";
  case ImageCallKind::Store2D:
    return "feme.cpu.image.store.2d.v4f32";
  case ImageCallKind::Store2DI32:
    return "feme.cpu.image.store.2d.v4i32";
  case ImageCallKind::Store2DArray:
    return "feme.cpu.image.store.2darray.v4f32";
  case ImageCallKind::Store2DArrayI32:
    return "feme.cpu.image.store.2darray.v4i32";
  case ImageCallKind::Load1D:
    return "feme.cpu.image.load.1d.v4f32";
  case ImageCallKind::Load1DI32:
    return "feme.cpu.image.load.1d.v4i32";
  case ImageCallKind::Store1D:
    return "feme.cpu.image.store.1d.v4f32";
  case ImageCallKind::Store1DI32:
    return "feme.cpu.image.store.1d.v4i32";
  case ImageCallKind::Load3D:
    return "feme.cpu.image.load.3d.v4f32";
  case ImageCallKind::Load3DI32:
    return "feme.cpu.image.load.3d.v4i32";
  case ImageCallKind::Store3D:
    return "feme.cpu.image.store.3d.v4f32";
  case ImageCallKind::Store3DI32:
    return "feme.cpu.image.store.3d.v4i32";
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
  Type *V4I32Ty = FixedVectorType::get(I32Ty, 4);

  FunctionType *FTy = nullptr;
  switch (Kind) {
  case ImageCallKind::Sample2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, dudx, dudy, dvdx, dvdy, lod,
    //  use_explicit_lod, mask) -> <4 x float>. `dudx`/`dudy`/`dvdx`/`dvdy`
    // (roadmap H7i) are the caller's own screen-space partial derivatives
    // of `(u, v)`, consulted only for an implicit-LOD sample
    // (`use_explicit_lod` false); a caller with none to give (a non-
    // fragment stage, or an explicit-LOD sample) passes zero constants.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty,
                             I1Ty},
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
    // (image_heap, image_heap_count, image_index, x, y, mip, sample, mask)
    // -> <4 x float>
    FTy = FunctionType::get(
        V4F32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load2DI32:
    // Same shape as Load2D, but returns <4 x i32> (roadmap E26).
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample2DArray:
    // Same as Sample2D, plus a float array_layer operand before lod.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, lod,
    //  use_explicit_lod, mask) -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I1Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Load2DArray:
    // Same as Load2D, plus an integer layer operand before mip.
    // (image_heap, image_heap_count, image_index, x, y, layer, mip,
    //  sample, mask) -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty,
                             I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Load2DArrayI32:
    // Same shape as Load2DArray, but returns <4 x i32>.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCube:
    // Same shape as Sample2D, but (u, v) becomes a 3-component direction
    // vector (dir_x, dir_y, dir_z).
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, lod,
    //  use_explicit_lod, mask) -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I1Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCubeArray:
    // Same as SampleCube, plus a float array_layer operand before lod.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2D:
    // (image_heap, image_heap_count, image_index, x, y, value, mask)
    // -> void
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DI32:
    // Same shape as Store2D, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DArray:
    // Same as Store2D, plus an integer layer operand before the texel
    // value (roadmap H19b).
    // (image_heap, image_heap_count, image_index, x, y, layer, value,
    //  mask) -> void
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DArrayI32:
    // Same shape as Store2DArray, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load1D:
    // (image_heap, image_heap_count, image_index, x, mip, sample, mask)
    // -> <4 x float> (roadmap H19c). Narrower than Load2D: a single
    // scalar `x` coordinate, no `y`.
    FTy = FunctionType::get(
        V4F32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load1DI32:
    // Same shape as Load1D, but returns <4 x i32>.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Store1D:
    // (image_heap, image_heap_count, image_index, x, value, mask) -> void
    // (roadmap H19c).
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store1DI32:
    // Same shape as Store1D, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load3D:
    // (image_heap, image_heap_count, image_index, x, y, z, mip, sample,
    //  mask) -> <4 x float> (roadmap H19c). Wider than Load2D: a third
    // `z` coordinate, never an array layer -- a 3D image is never
    // arrayed.
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load3DI32:
    // Same shape as Load3D, but returns <4 x i32>.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store3D:
    // (image_heap, image_heap_count, image_index, x, y, z, value, mask)
    // -> void (roadmap H19c).
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store3DI32:
    // Same shape as Store3D, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  }

  StringRef Name = getImageCallName(Kind);
  Function *F = cast<Function>(M.getOrInsertFunction(Name, FTy).getCallee());
  if (!F->hasFnAttribute(Attribute::Memory)) {
    // Every `feme.cpu.image.*` read-only call only reads through its heap
    // pointer arguments (see `feme::cpu::ResourceCalls::getOrInsertResourceCall`'s
    // identical reasoning for buffers); `Store2D`/`Store2DI32` (roadmap
    // H19a) instead only *write* through theirs.
    bool IsStore = Kind == ImageCallKind::Store2D ||
                   Kind == ImageCallKind::Store2DI32 ||
                   Kind == ImageCallKind::Store2DArray ||
                   Kind == ImageCallKind::Store2DArrayI32 ||
                   Kind == ImageCallKind::Store1D ||
                   Kind == ImageCallKind::Store1DI32 ||
                   Kind == ImageCallKind::Store3D ||
                   Kind == ImageCallKind::Store3DI32;
    F->setMemoryEffects(MemoryEffects::argMemOnly(
        IsStore ? ModRefInfo::Mod : ModRefInfo::Ref));
    F->setWillReturn();
    F->setDoesNotThrow();
  }
  return F;
}

CallInst *feme::cpu::createSample2D(IRBuilderBase &Builder,
                                    const ImageCallEnv &Env, Value *ImageIndex,
                                    Value *SamplerIndex, Value *U, Value *V,
                                    Value *DUdX, Value *DUdY, Value *DVdX,
                                    Value *DVdY, Value *Lod,
                                    Value *UseExplicitLod, Value *Mask,
                                    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, DUdX, DUdY, DVdX, DVdY, Lod, UseExplicitLod,
                             Mask},
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
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, Lod, UseExplicitLod, Dref, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad2D(IRBuilderBase &Builder,
                                  const ImageCallEnv &Env, Value *ImageIndex,
                                  Value *X, Value *Y, Value *Mip,
                                  Value *Sample, Value *Mask,
                                  const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Mip, Sample, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad2DI32(IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     Value *ImageIndex, Value *X, Value *Y,
                                     Value *Mip, Value *Mask,
                                     const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Mip, Mask},
      Name);
}

CallInst *feme::cpu::createStore2D(IRBuilderBase &Builder,
                                   const ImageCallEnv &Env, Value *ImageIndex,
                                   Value *X, Value *Y, Value *Texel,
                                   Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Texel, Mask},
      Name);
}

CallInst *feme::cpu::createStore2DI32(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *X, Value *Y,
                                      Value *Texel, Value *Mask,
                                      const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Texel, Mask},
      Name);
}

CallInst *feme::cpu::createStore2DArray(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X, Value *Y,
                                        Value *Layer, Value *Texel,
                                        Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createStore2DArrayI32(IRBuilderBase &Builder,
                                           const ImageCallEnv &Env,
                                           Value *ImageIndex, Value *X,
                                           Value *Y, Value *Layer,
                                           Value *Texel, Value *Mask,
                                           const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DArrayI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createSample2DArray(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex,
                                         Value *SamplerIndex, Value *U,
                                         Value *V, Value *ArrayLayer,
                                         Value *Lod, Value *UseExplicitLod,
                                         Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, ArrayLayer, Lod, UseExplicitLod, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad2DArray(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X, Value *Y,
                                       Value *Layer, Value *Mip, Value *Sample,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Mip, Sample, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad2DArrayI32(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex, Value *X,
                                         Value *Y, Value *Layer, Value *Mip,
                                         Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2DArrayI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Mip, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCube(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *SamplerIndex,
                                      Value *DirX, Value *DirY, Value *DirZ,
                                      Value *Lod, Value *UseExplicitLod,
                                      Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, Lod, UseExplicitLod, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCubeArray(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ,
    Value *ArrayLayer, Value *Lod, Value *UseExplicitLod, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCubeArray);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
       Env.SamplerHeapCount, ImageIndex, SamplerIndex, DirX, DirY, DirZ,
       ArrayLayer, Lod, UseExplicitLod, Mask},
      Name);
}

CallInst *feme::cpu::createLoad1D(IRBuilderBase &Builder,
                                  const ImageCallEnv &Env, Value *ImageIndex,
                                  Value *X, Value *Mip, Value *Sample,
                                  Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load1D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Mip, Sample, Mask},
      Name);
}

CallInst *feme::cpu::createLoad1DI32(IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     Value *ImageIndex, Value *X, Value *Mip,
                                     Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load1DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Mip, Mask}, Name);
}

CallInst *feme::cpu::createStore1D(IRBuilderBase &Builder,
                                   const ImageCallEnv &Env, Value *ImageIndex,
                                   Value *X, Value *Texel, Value *Mask,
                                   const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store1D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Texel, Mask},
      Name);
}

CallInst *feme::cpu::createStore1DI32(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *X,
                                      Value *Texel, Value *Mask,
                                      const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store1DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Texel, Mask},
      Name);
}

CallInst *feme::cpu::createLoad3D(IRBuilderBase &Builder,
                                  const ImageCallEnv &Env, Value *ImageIndex,
                                  Value *X, Value *Y, Value *Z, Value *Mip,
                                  Value *Sample, Value *Mask,
                                  const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load3D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Z, Mip, Sample, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad3DI32(IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     Value *ImageIndex, Value *X, Value *Y,
                                     Value *Z, Value *Mip, Value *Mask,
                                     const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load3DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Z, Mip, Mask},
      Name);
}

CallInst *feme::cpu::createStore3D(IRBuilderBase &Builder,
                                   const ImageCallEnv &Env, Value *ImageIndex,
                                   Value *X, Value *Y, Value *Z, Value *Texel,
                                   Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store3D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Z, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createStore3DI32(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *X, Value *Y,
                                      Value *Z, Value *Texel, Value *Mask,
                                      const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store3DI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Z, Texel, Mask},
                            Name);
}

std::optional<MatchedImageCall> feme::cpu::matchImageCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  static constexpr ImageCallKind AllKinds[] = {
      ImageCallKind::Sample2D,      ImageCallKind::SampleCmp2D,
      ImageCallKind::Load2D,        ImageCallKind::Load2DI32,
      ImageCallKind::Sample2DArray, ImageCallKind::Load2DArray,
      ImageCallKind::Load2DArrayI32, ImageCallKind::SampleCube,
      ImageCallKind::SampleCubeArray, ImageCallKind::Store2D,
      ImageCallKind::Store2DI32, ImageCallKind::Store2DArray,
      ImageCallKind::Store2DArrayI32, ImageCallKind::Load1D,
      ImageCallKind::Load1DI32, ImageCallKind::Store1D,
      ImageCallKind::Store1DI32, ImageCallKind::Load3D,
      ImageCallKind::Load3DI32, ImageCallKind::Store3D,
      ImageCallKind::Store3DI32};

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
    if (CI.arg_size() != 15)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.DUdX = CI.getArgOperand(8);
    Result.DUdY = CI.getArgOperand(9);
    Result.DVdX = CI.getArgOperand(10);
    Result.DVdY = CI.getArgOperand(11);
    Result.Lod = CI.getArgOperand(12);
    Result.UseExplicitLod = CI.getArgOperand(13);
    Result.Mask = CI.getArgOperand(14);
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
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Lod = CI.getArgOperand(5);
    Result.Sample = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::Load2DI32:
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
  case ImageCallKind::Sample2DArray:
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
    Result.ArrayLayer = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.UseExplicitLod = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::Load2DArray:
    if (CI.arg_size() != 9)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Layer = CI.getArgOperand(5);
    Result.Lod = CI.getArgOperand(6);
    Result.Sample = CI.getArgOperand(7);
    Result.Mask = CI.getArgOperand(8);
    break;
  case ImageCallKind::Load2DArrayI32:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Layer = CI.getArgOperand(5);
    Result.Lod = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::SampleCube:
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
    Result.W = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.UseExplicitLod = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::SampleCubeArray:
    if (CI.arg_size() != 13)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.W = CI.getArgOperand(8);
    Result.ArrayLayer = CI.getArgOperand(9);
    Result.Lod = CI.getArgOperand(10);
    Result.UseExplicitLod = CI.getArgOperand(11);
    Result.Mask = CI.getArgOperand(12);
    break;
  case ImageCallKind::Store2D:
  case ImageCallKind::Store2DI32:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Texel = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  case ImageCallKind::Store2DArray:
  case ImageCallKind::Store2DArrayI32:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Layer = CI.getArgOperand(5);
    Result.Texel = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::Load1D:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Lod = CI.getArgOperand(4);
    Result.Sample = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  case ImageCallKind::Load1DI32:
    if (CI.arg_size() != 6)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Lod = CI.getArgOperand(4);
    Result.Mask = CI.getArgOperand(5);
    break;
  case ImageCallKind::Store1D:
  case ImageCallKind::Store1DI32:
    if (CI.arg_size() != 6)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Texel = CI.getArgOperand(4);
    Result.Mask = CI.getArgOperand(5);
    break;
  case ImageCallKind::Load3D:
    if (CI.arg_size() != 9)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Z = CI.getArgOperand(5);
    Result.Lod = CI.getArgOperand(6);
    Result.Sample = CI.getArgOperand(7);
    Result.Mask = CI.getArgOperand(8);
    break;
  case ImageCallKind::Load3DI32:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Z = CI.getArgOperand(5);
    Result.Lod = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::Store3D:
  case ImageCallKind::Store3DI32:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Z = CI.getArgOperand(5);
    Result.Texel = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  }
  return Result;
}

SampleDerivatives
feme::cpu::getOrSynthesizeSample2DDerivatives(IRBuilderBase &B,
                                              Function &Caller, Value *U,
                                              Value *V) {
  std::optional<feme::ShaderStage> Stage = feme::getShaderStage(Caller);
  if (Stage != feme::ShaderStage::Fragment) {
    Value *Zero = ConstantFP::get(B.getFloatTy(), 0.0);
    return {Zero, Zero, Zero, Zero};
  }
  return {feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      U),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      U),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      V),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      V)};
}
