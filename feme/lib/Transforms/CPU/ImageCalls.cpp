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
  case ImageCallKind::Load1DArray:
    return "feme.cpu.image.load.1darray.v4f32";
  case ImageCallKind::Load1DArrayI32:
    return "feme.cpu.image.load.1darray.v4i32";
  case ImageCallKind::Store1DArray:
    return "feme.cpu.image.store.1darray.v4f32";
  case ImageCallKind::Store1DArrayI32:
    return "feme.cpu.image.store.1darray.v4i32";
  case ImageCallKind::Store2DMS:
    return "feme.cpu.image.store.2dms.v4f32";
  case ImageCallKind::Store2DMSI32:
    return "feme.cpu.image.store.2dms.v4i32";
  case ImageCallKind::Store2DArrayMS:
    return "feme.cpu.image.store.2darrayms.v4f32";
  case ImageCallKind::Store2DArrayMSI32:
    return "feme.cpu.image.store.2darrayms.v4i32";
  case ImageCallKind::AtomicAdd2D:
    return "feme.cpu.image.atomic.add.2d.i32";
  case ImageCallKind::AtomicSub2D:
    return "feme.cpu.image.atomic.sub.2d.i32";
  case ImageCallKind::AtomicAnd2D:
    return "feme.cpu.image.atomic.and.2d.i32";
  case ImageCallKind::AtomicOr2D:
    return "feme.cpu.image.atomic.or.2d.i32";
  case ImageCallKind::AtomicXor2D:
    return "feme.cpu.image.atomic.xor.2d.i32";
  case ImageCallKind::AtomicSMax2D:
    return "feme.cpu.image.atomic.smax.2d.i32";
  case ImageCallKind::AtomicSMin2D:
    return "feme.cpu.image.atomic.smin.2d.i32";
  case ImageCallKind::AtomicUMax2D:
    return "feme.cpu.image.atomic.umax.2d.i32";
  case ImageCallKind::AtomicUMin2D:
    return "feme.cpu.image.atomic.umin.2d.i32";
  case ImageCallKind::AtomicExchange2D:
    return "feme.cpu.image.atomic.exchange.2d.i32";
  case ImageCallKind::AtomicCompareExchange2D:
    return "feme.cpu.image.atomic.compare_exchange.2d.i32";
  case ImageCallKind::SampleCmpArray2D:
    return "feme.cpu.image.samplecmp.2darray.f32";
  case ImageCallKind::SampleCmpCube:
    return "feme.cpu.image.samplecmp.cube.f32";
  case ImageCallKind::SampleCmpCubeArray:
    return "feme.cpu.image.samplecmp.cubearray.f32";
  case ImageCallKind::Sample1D:
    return "feme.cpu.image.sample.1d.v4f32";
  case ImageCallKind::Sample1DArray:
    return "feme.cpu.image.sample.1darray.v4f32";
  case ImageCallKind::SampleCmp1D:
    return "feme.cpu.image.samplecmp.1d.f32";
  case ImageCallKind::SampleCmpArray1D:
    return "feme.cpu.image.samplecmp.1darray.f32";
  case ImageCallKind::QueryLod2D:
    return "feme.cpu.image.querylod.2d.v2f32";
  case ImageCallKind::QueryLodCube:
    return "feme.cpu.image.querylod.cube.v2f32";
  case ImageCallKind::Sample3D:
    return "feme.cpu.image.sample.3d.v4f32";
  case ImageCallKind::GetDimensions2D:
    return "feme.cpu.image.getdimensions.2d.v2i32";
  case ImageCallKind::QuerySizeLod2D:
    return "feme.cpu.image.getdimensions.lod.2d.v2i32";
  case ImageCallKind::QuerySizeLod1D:
    return "feme.cpu.image.getdimensions.lod.1d.i32";
  case ImageCallKind::QuerySizeLod1DArray:
    return "feme.cpu.image.getdimensions.lod.1darray.v2i32";
  case ImageCallKind::QuerySizeLod2DArray:
    return "feme.cpu.image.getdimensions.lod.2darray.v3i32";
  case ImageCallKind::QuerySizeLod3D:
    return "feme.cpu.image.getdimensions.lod.3d.v3i32";
  case ImageCallKind::QuerySizeLodCubeArray:
    return "feme.cpu.image.getdimensions.lod.cubearray.v3i32";
  case ImageCallKind::QueryLevels:
    return "feme.cpu.image.querylevels.i32";
  case ImageCallKind::QuerySamples:
    return "feme.cpu.image.querysamples.i32";
  case ImageCallKind::GatherCmp2D:
    return "feme.cpu.image.gathercmp.2d.v4f32";
  case ImageCallKind::Gather2D:
    return "feme.cpu.image.gather.2d.v4f32";
  case ImageCallKind::GatherCmpArray2D:
    return "feme.cpu.image.gathercmp.array2d.v4f32";
  case ImageCallKind::GatherArray2D:
    return "feme.cpu.image.gather.array2d.v4f32";
  case ImageCallKind::Sample2DI32:
    return "feme.cpu.image.sample.2d.v4i32";
  case ImageCallKind::GatherCmpCube:
    return "feme.cpu.image.gathercmp.cube.v4f32";
  case ImageCallKind::GatherCube:
    return "feme.cpu.image.gather.cube.v4f32";
  case ImageCallKind::Gather2DI32:
    return "feme.cpu.image.gather.2d.v4i32";
  case ImageCallKind::GatherArray2DI32:
    return "feme.cpu.image.gather.array2d.v4i32";
  case ImageCallKind::GatherCubeI32:
    return "feme.cpu.image.gather.cube.v4i32";
  case ImageCallKind::Gather2DOffsets:
    return "feme.cpu.image.gather.2d.offsets.v4f32";
  case ImageCallKind::GatherArray2DOffsets:
    return "feme.cpu.image.gather.array2d.offsets.v4f32";
  case ImageCallKind::Gather2DOffsetsI32:
    return "feme.cpu.image.gather.2d.offsets.v4i32";
  case ImageCallKind::GatherArray2DOffsetsI32:
    return "feme.cpu.image.gather.array2d.offsets.v4i32";
  case ImageCallKind::Sample1DI32:
    return "feme.cpu.image.sample.1d.v4i32";
  case ImageCallKind::Sample1DArrayI32:
    return "feme.cpu.image.sample.1darray.v4i32";
  case ImageCallKind::Sample2DArrayI32:
    return "feme.cpu.image.sample.2darray.v4i32";
  case ImageCallKind::Sample3DI32:
    return "feme.cpu.image.sample.3d.v4i32";
  case ImageCallKind::SampleCubeI32:
    return "feme.cpu.image.sample.cube.v4i32";
  case ImageCallKind::SampleCubeArrayI32:
    return "feme.cpu.image.sample.cubearray.v4i32";
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
    //  use_explicit_lod, bias, offset_x, offset_y, min_lod_clamp, mask) ->
    //  <4 x float>. `dudx`/`dudy`/`dvdx`/`dvdy` (roadmap H7i) are the
    // caller's own screen-space partial derivatives of `(u, v)`,
    // consulted only for an implicit-LOD sample (`use_explicit_lod`
    // false); a caller with none to give (a non-fragment stage, or an
    // explicit-LOD sample) passes zero constants. `bias` (roadmap L58) is
    // SPIR-V's own `Bias` image operand, added to the raw implicit LOD
    // before the sampler's own bias/clamp runs; a caller with none to
    // give (including every explicit-LOD sample) passes a zero constant.
    // `offset_x`/`offset_y`
    // (roadmap L26) are SPIR-V's own `ConstOffset` image operand, a
    // compile-time-constant integer texel offset added to every fetched
    // texel's own address before the sampler's addressing mode is
    // applied; a caller with none to give (DXIL) passes zero constants.
    // `min_lod_clamp` (roadmap L26) is SPIR-V's own `MinLod` image
    // operand (HLSL's `Texture2D::Sample`'s trailing `clamp` argument),
    // an additional floor on the implicit LOD alongside the sampler's own
    // `minLod`; a caller with none to give passes negative infinity (a
    // no-op floor).
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty,
                             F32Ty, I32Ty, I32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmp2D:
    // Same as Sample2D's own (image_heap..v) prefix, plus
    // `dudx`/`dudy`/`dvdx`/`dvdy` (roadmap L66(c); SPIR-V's own `Grad`
    // image operand pair, mirroring Sample2D's own operands of the same
    // names -- a caller with none to give passes zero constants, which
    // `femeCpuImageSampleCmp2DF32` provably lowers to the exact same
    // level-0 result its own narrower pre-L66(c) implementation always
    // computed), a trailing `dref`, `bias` (roadmap L52(b); SPIR-V's own
    // `Bias` image operand, mirroring Sample2D's own operand of the same
    // name), `offset_x`/`offset_y` (roadmap L50d; SPIR-V's own
    // `ConstOffset` image operand, mirroring Sample2D's own operand of
    // the same name), `min_lod_clamp` (roadmap L52(c); SPIR-V's own
    // `MinLod` image operand, mirroring Sample2D's own operand of the
    // same name), and `mask`; returns `float`.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, dudx, dudy, dvdx, dvdy, lod,
    //  use_explicit_lod, dref, bias, offset_x, offset_y, min_lod_clamp,
    //  mask) -> float
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty,
                             F32Ty, F32Ty, I32Ty, I32Ty, F32Ty, I1Ty},
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
    // Same shape as Load2D, but returns <4 x i32> (roadmap E26); adds
    // Load2D's own trailing sample operand (roadmap H19g).
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample2DArray:
    // Same as Sample2D, plus a float array_layer operand before the
    // derivatives. Roadmap L60(a): gains its own dudx/dudy/dvdx/dvdy,
    // bias, and min_lod_clamp operands, mirroring Sample2D's own. Roadmap
    // L33: gains its own offset_x/offset_y operand pair too, mirroring
    // Sample2D's own identically-named operands (an ordinary Array2D
    // sample's own ConstOffset lowering was deferred past L26/L60(a), but
    // is no longer future work).
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, dudx, dudy, dvdx,
    //  dvdy, lod, use_explicit_lod, bias, offset_x, offset_y,
    //  min_lod_clamp, mask) -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
                             I1Ty,  F32Ty, I32Ty, I32Ty, F32Ty, I1Ty},
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
    // Same shape as Load2DArray, but returns <4 x i32>. Roadmap H19m:
    // widened in place to add the same integer `sample` operand before
    // `mask` that `Load2DArray` (float) already had -- mirroring how
    // roadmap H19g widened `Load2DI32` to add the operand `Load2D` already
    // had.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCube:
    // Same shape as Sample2D, but (u, v) becomes a 3-component direction
    // vector (dir_x, dir_y, dir_z), and there is no offset (SPIR-V
    // forbids `ConstOffset` against `Dim::Cube`, see
    // `isSupportedOffset`'s own comment). Roadmap L56: gains its own six
    // screen-space partial-derivative operands, mirroring Sample2D's own
    // `dudx`/`dudy`/`dvdx`/`dvdy` (roadmap H7i) but for all three
    // direction-vector components, since which face (and therefore which
    // face-local 2D coordinate) a given direction resolves to isn't known
    // until the runtime sees concrete values -- see
    // `getOrSynthesizeSampleCubeDerivatives`'s doc. Roadmap L58: gains the
    // same `bias` operand Sample2D's own does.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, ddirxdx, ddirxdy,
    //  ddirydx, ddirydy, ddirzdx, ddirzdy, lod, use_explicit_lod, bias,
    //  min_lod_clamp, mask) -> <4 x float>
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCubeArray:
    // Same as SampleCube (including its own roadmap L56 derivative
    // operands and roadmap L58/L60(a) bias/min_lod_clamp operands), plus
    // a float array_layer operand before lod.
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, F32Ty,
         I1Ty},
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
  case ImageCallKind::Load1DArray:
    // (image_heap, image_heap_count, image_index, x, layer, mip, sample,
    //  mask) -> <4 x float> (roadmap H19e). Same shape as Load1D, plus an
    // integer layer operand before mip -- mirroring Load2DArray's own
    // extension of Load2D.
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Load1DArrayI32:
    // Same shape as Load1DArray, but returns <4 x i32>.
    FTy = FunctionType::get(
        V4I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store1DArray:
    // (image_heap, image_heap_count, image_index, x, layer, value, mask)
    // -> void (roadmap H19e).
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store1DArrayI32:
    // Same shape as Store1DArray, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DMS:
    // (image_heap, image_heap_count, image_index, x, y, sample, value,
    //  mask) -> void (roadmap H19g). Same shape as Store2DArray, but the
    // 3rd coordinate operand is a sample index rather than an array layer.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DMSI32:
    // Same shape as Store2DMS, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DArrayMS:
    // (image_heap, image_heap_count, image_index, x, y, layer, sample,
    //  value, mask) -> void (roadmap H19m): both `Store2DArray`'s own
    // layer operand and `Store2DMS`'s own sample operand, together.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Store2DArrayMSI32:
    // Same shape as Store2DArrayMS, but the value operand is <4 x i32>.
    FTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, V4I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::AtomicAdd2D:
  case ImageCallKind::AtomicSub2D:
  case ImageCallKind::AtomicAnd2D:
  case ImageCallKind::AtomicOr2D:
  case ImageCallKind::AtomicXor2D:
  case ImageCallKind::AtomicSMax2D:
  case ImageCallKind::AtomicSMin2D:
  case ImageCallKind::AtomicUMax2D:
  case ImageCallKind::AtomicUMin2D:
  case ImageCallKind::AtomicExchange2D:
    // (image_heap, image_heap_count, image_index, x, y, value, mask)
    // -> i32 (roadmap H8v): unlike every Load*/Store* kind above, this
    // returns the pre-op scalar rather than reading/writing a <4 x ?32>
    // texel.
    FTy = FunctionType::get(
        I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::AtomicCompareExchange2D:
    // (image_heap, image_heap_count, image_index, x, y, comparator, value,
    //  mask) -> i32 (roadmap H8v): AtomicAdd2D's own shape, plus a
    // leading comparator operand before value.
    FTy = FunctionType::get(
        I32Ty,
        {PtrTy, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmpArray2D:
    // Same as SampleCmp2D, plus a float array_layer operand before
    // `dudx`/`dudy`/`dvdx`/`dvdy` (roadmap L48, mirroring Sample2DArray's
    // own relationship to Sample2D); `dudx`/`dudy`/`dvdx`/`dvdy` (roadmap
    // L66(g)) mirror SampleCmp2D's own identically-named `Grad` operands
    // -- only `u`/`v`, never `array_layer`, is ever differentiated,
    // matching Sample2DArray's own identical precedent for an ordinary
    // sample.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, dudx, dudy, dvdx,
    //  dvdy, lod, use_explicit_lod, dref, bias, offset_x, offset_y,
    //  min_lod_clamp, mask) -> float
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
                             I1Ty,  F32Ty, F32Ty, I32Ty, I32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmpCube:
    // Same as SampleCmp2D, but (u, v) becomes a 3-component direction
    // vector (dir_x, dir_y, dir_z), mirroring SampleCube's own
    // relationship to Sample2D (roadmap L48); no offset (SPIR-V forbids
    // `ConstOffset` against `Dim::Cube`), but `min_lod_clamp` (roadmap
    // L52(c)) is still legal. Roadmap L66(h) adds a real
    // ddir_x_dx/ddir_x_dy/ddir_y_dx/ddir_y_dy/ddir_z_dx/ddir_z_dy
    // direction-vector derivative sextuple between dir_z and lod,
    // mirroring SampleCube's own identically-placed derivative operands.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, ddir_x_dx,
    //  ddir_x_dy, ddir_y_dx, ddir_y_dy, ddir_z_dx, ddir_z_dy, lod,
    //  use_explicit_lod, dref, bias, min_lod_clamp, mask) -> float
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
                             F32Ty, F32Ty, I1Ty,  F32Ty, F32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmpCubeArray:
    // Same as SampleCmpCube, plus a float array_layer operand before lod,
    // mirroring SampleCubeArray's own relationship to SampleCube (roadmap
    // L48). Roadmap L66(i) adds a real ddir_x_dx/ddir_x_dy/ddir_y_dx/
    // ddir_y_dy/ddir_z_dx/ddir_z_dy direction-vector derivative sextuple
    // between dir_z and array_layer, mirroring SampleCmpCube's own
    // identically-placed derivative operands (roadmap L66(h)).
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, ddir_x_dx,
    //  ddir_x_dy, ddir_y_dx, ddir_y_dy, ddir_z_dx, ddir_z_dy, array_layer,
    //  lod, use_explicit_lod, dref, bias, min_lod_clamp, mask) -> float
    FTy = FunctionType::get(F32Ty, {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty,
                                    F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
                                    F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty,
                                    F32Ty, F32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample1D:
    // Roadmap L52a: a single float u coordinate, no offset originally
    // (mirroring Sample2DArray's own simpler scope). Roadmap L61(c) adds
    // a real Bias/min_lod_clamp pair, mirroring Sample2D's own operand
    // order (lod, use_explicit_lod, bias, ..., min_lod_clamp, mask).
    // Roadmap L63 adds a real du_dx/du_dy screen-space derivative pair,
    // mirroring Sample2D's own du_dx/du_dy/dv_dx/dv_dy placement
    // immediately after the coordinate. Roadmap L66(d) adds a real,
    // bare-scalar offset operand between bias and min_lod_clamp,
    // mirroring Sample2D's own OffsetX/OffsetY placement but narrowed to
    // a single i32 (this shape's own ConstOffset is a scalar, not a
    // vector -- see createSample1D's own doc).
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, du_dx, du_dy, lod, use_explicit_lod,
    //  bias, offset, min_lod_clamp, mask) -> <4 x float>
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, I32Ty, F32Ty,
                             I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample1DArray:
    // Same as Sample1D, plus a float array_layer operand before du_dx,
    // mirroring Sample2DArray's own relationship to Sample2D.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, I32Ty,
                             F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmp1D:
    // Roadmap L54: the depth-comparison counterpart of Sample1D. Roadmap
    // L62 adds a real bias/min_lod_clamp pair, mirroring SampleCmpCube's
    // own operand order (lod, use_explicit_lod, dref, bias,
    // min_lod_clamp). Roadmap L66(f) adds a real du_dx/du_dy scalar
    // derivative pair between u and lod, mirroring Sample1D's own
    // du_dx/du_dy placement. Roadmap L66(k) adds a real, bare-scalar
    // offset operand between bias and min_lod_clamp, mirroring Sample1D's
    // own offset placement -- a real deqp-vk SPIR-V capture confirms this
    // shape's own depth-comparison ConstOffset is the same bare scalar an
    // ordinary sample's is, despite its own dref-widened Coordinate
    // staying a genuine vector.
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, du_dx, du_dy, lod, use_explicit_lod,
    //  dref, bias, offset, min_lod_clamp, mask) -> float
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, F32Ty, I32Ty,
                             F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCmpArray1D:
    // Same as SampleCmp1D, plus a float array_layer operand before
    // du_dx, mirroring Sample1DArray's own relationship to Sample1D.
    FTy = FunctionType::get(F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, F32Ty,
                             I32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::QueryLod2D: {
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dudx, dudy, dvdx, dvdy, mask) ->
    //  <2 x float> (roadmap L52e): lane 0 the clamped level, lane 1 the
    // raw unclamped LOD -- see `ImageCallKind::QueryLod2D`'s own doc.
    // Unlike Sample2D, there is no coordinate (`u`/`v`) operand at all:
    // `femeCpuImageQueryLod2DV2F32` never needs the coordinate itself,
    // only its derivatives.
    Type *V2F32Ty = FixedVectorType::get(F32Ty, 2);
    FTy = FunctionType::get(
        V2F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         I1Ty},
        /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QueryLodCube: {
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, ddir_x_dx,
    //  ddir_x_dy, ddir_y_dx, ddir_y_dy, ddir_z_dx, ddir_z_dy, mask) ->
    //  <2 x float> (roadmap H124u): same lane convention as QueryLod2D.
    // Unlike QueryLod2D, the direction vector itself is a real operand
    // (needed for face selection), not just its derivatives -- see
    // `ImageCallKind::QueryLodCube`'s own doc.
    Type *V2F32Ty = FixedVectorType::get(F32Ty, 2);
    FTy = FunctionType::get(V2F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
                             F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::Sample3D:
    // Roadmap L66(a): a real `(u, v, w)` coordinate plus its own
    // `du_dx`/`du_dy`/`dv_dx`/`dv_dy`/`dw_dx`/`dw_dy` screen-space
    // derivative triple (consulted only for an implicit-LOD sample,
    // mirroring `Sample1D`'s own `du_dx`/`du_dy` pair). Roadmap L67(a)
    // adds a real `bias`/`min_lod_clamp` pair, mirroring `Sample1D`'s own
    // operand order (lod, use_explicit_lod, bias, min_lod_clamp, mask).
    // Roadmap L67(c) adds a real `offset_x`/`offset_y`/`offset_z` triple
    // between `bias` and `min_lod_clamp`, mirroring `Sample2D`'s own
    // `offset_x`/`offset_y` placement, widened to a real third, depth-axis
    // component (`Plain3D`'s own `ConstOffset` is a genuine `<3 x i32>`,
    // matching its own 3-component coordinate width).
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, w, du_dx, du_dy, dv_dx, dv_dy,
    //  dw_dx, dw_dy, lod, use_explicit_lod, bias, offset_x, offset_y,
    //  offset_z, min_lod_clamp, mask) -> <4 x float>
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, F32Ty, I1Ty, F32Ty, I32Ty, I32Ty,
         I32Ty, F32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::GetDimensions2D: {
    // (image_heap, image_heap_count, image_index, mask) -> <2 x i32>
    // (roadmap L70): lane 0 the mip-0 width, lane 1 the mip-0 height --
    // see `ImageCallKind::GetDimensions2D`'s own doc. No sampler heap and
    // no coordinate operand at all, unlike every sample/fetch call above.
    Type *V2I32Ty = FixedVectorType::get(I32Ty, 2);
    FTy = FunctionType::get(V2I32Ty, {PtrTy, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QuerySizeLod2D: {
    // (image_heap, image_heap_count, image_index, lod, mask) -> <2 x i32>
    // (roadmap L72(d)): same shape as `GetDimensions2D` plus one more
    // operand, the explicit mip level to query -- see
    // `ImageCallKind::QuerySizeLod2D`'s own doc.
    Type *V2I32Ty = FixedVectorType::get(I32Ty, 2);
    FTy = FunctionType::get(V2I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QuerySizeLod1D:
    // (image_heap, image_heap_count, image_index, lod, mask) -> i32
    // (roadmap L75): same operand list as `QuerySizeLod2D` above, but a
    // bare scalar result -- see `ImageCallKind::QuerySizeLod1D`'s own doc.
    FTy = FunctionType::get(I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::QuerySizeLod1DArray: {
    // (image_heap, image_heap_count, image_index, lod, mask) -> <2 x i32>
    // (roadmap L75): same operand list/result width as `QuerySizeLod2D`,
    // but a different per-lane formula -- see
    // `ImageCallKind::QuerySizeLod1DArray`'s own doc.
    Type *V2I32Ty = FixedVectorType::get(I32Ty, 2);
    FTy = FunctionType::get(V2I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QuerySizeLod2DArray: {
    // (image_heap, image_heap_count, image_index, lod, mask) -> <3 x i32>
    // (roadmap L75): see `ImageCallKind::QuerySizeLod2DArray`'s own doc.
    Type *V3I32Ty = FixedVectorType::get(I32Ty, 3);
    FTy = FunctionType::get(V3I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QuerySizeLod3D: {
    // (image_heap, image_heap_count, image_index, lod, mask) -> <3 x i32>
    // (roadmap L75): see `ImageCallKind::QuerySizeLod3D`'s own doc.
    Type *V3I32Ty = FixedVectorType::get(I32Ty, 3);
    FTy = FunctionType::get(V3I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QuerySizeLodCubeArray: {
    // (image_heap, image_heap_count, image_index, lod, mask) -> <3 x i32>
    // (roadmap L75): see `ImageCallKind::QuerySizeLodCubeArray`'s own
    // doc.
    Type *V3I32Ty = FixedVectorType::get(I32Ty, 3);
    FTy = FunctionType::get(V3I32Ty, {PtrTy, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  }
  case ImageCallKind::QueryLevels:
    // (image_heap, image_heap_count, image_index) -> i32 (roadmap
    // L72(d)): the smallest operand list of any `feme.cpu.image.*` call
    // -- no `Mask` (no per-invocation side effect to guard against) and
    // no coordinate/mip-level operand at all -- see
    // `ImageCallKind::QueryLevels`'s own doc.
    FTy = FunctionType::get(I32Ty, {PtrTy, I32Ty, I32Ty}, /*isVarArg=*/false);
    break;
  case ImageCallKind::QuerySamples:
    // (image_heap, image_heap_count, image_index) -> i32 (roadmap L73):
    // identical operand list/shape to `QueryLevels` above -- see
    // `ImageCallKind::QuerySamples`'s own doc.
    FTy = FunctionType::get(I32Ty, {PtrTy, I32Ty, I32Ty}, /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherCmp2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, dref, offset_x, offset_y, mask)
    //  -> <4 x float> (roadmap L7d): see `ImageCallKind::GatherCmp2D`'s
    // own doc -- no `lod`/`use_explicit_lod`/`bias`/`dudx`/`dudy`/`dvdx`/
    // `dvdy`/`min_lod_clamp` operand at all, unlike `SampleCmp2D`, since
    // a gather instruction always operates at mip level 0 per the SPIR-V
    // spec, with no way to request otherwise.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Gather2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, component, offset_x, offset_y,
    //  mask) -> <4 x float> (roadmap L7g): identical operand shape to
    // `GatherCmp2D` above, except `component` (which of the four sampled
    // texel components -- R/G/B/A -- to gather) is an `i32` selector
    // rather than a `dref` (`f32`) comparison reference.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherCmpArray2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, dref, offset_x,
    //  offset_y, mask) -> <4 x float> (roadmap H124q): the `Array2D`
    // counterpart of `GatherCmp2D` above, adding `array_layer` (`f32`,
    // mirroring `Sample2DArray`'s own identical operand) right after `v`.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherArray2D:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, component,
    //  offset_x, offset_y, mask) -> <4 x float> (roadmap H124q): the
    // `Array2D` counterpart of `Gather2D` above, adding `array_layer`
    // the same way `GatherCmpArray2D` does to `GatherCmp2D`.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample2DI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, lod, offset_x, offset_y, mask) ->
    //  <4 x i32> (roadmap H109): the integer-channel counterpart of
    // `Sample2D`'s own operand list, minus `dudx`/`dudy`/`dvdx`/`dvdy`
    // (this kind is explicit-LOD only -- see `ImageCallKind::Sample2DI32`'s
    // own doc for why) and minus `use_explicit_lod`/`bias`/`min_lod_clamp`
    // (always implicitly true/unused/no-op for the same reason).
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, I32Ty,
         I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherCmpCube:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, dref, mask) ->
    //  <4 x float> (roadmap H124r): the `TextureCube` counterpart of
    // `GatherCmp2D` above -- `dir_x`/`dir_y`/`dir_z` (a 3-component
    // direction vector, mirroring `SampleCube`'s own convention) in
    // place of `u`/`v`, and no `offset_x`/`offset_y` operand at all,
    // since SPIR-V forbids `ConstOffset` against `Dim::Cube` outright.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherCube:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, component, mask)
    //  -> <4 x float> (roadmap H124r): identical operand shape to
    // `GatherCmpCube` above, except `component` (an `i32` R/G/B/A
    // selector) replaces `dref`, mirroring `Gather2D`'s own relationship
    // to `GatherCmp2D`.
    FTy = FunctionType::get(V4F32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Gather2DI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, component, offset_x, offset_y,
    //  mask) -> <4 x i32> (roadmap L125(k)): identical operand shape to
    // `Gather2D` above, but returning `<4 x i32>` for an integer-channel
    // (`usampler2D`/`isampler2D`) sampled image.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherArray2DI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, component,
    //  offset_x, offset_y, mask) -> <4 x i32> (roadmap L125(k)): the
    // `Array2D` counterpart of `Gather2DI32` above, adding `array_layer`
    // the same way `GatherArray2D` does to `Gather2D`.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherCubeI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, component, mask)
    //  -> <4 x i32> (roadmap L125(k)): the `TextureCube` counterpart of
    // `Gather2DI32` above, mirroring `GatherCube`'s own direction-vector
    // coordinate and lack of an offset operand.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Gather2DOffsets:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, component, offset_x0, offset_y0,
    //  offset_x1, offset_y1, offset_x2, offset_y2, offset_x3, offset_y3,
    //  mask) -> <4 x float> (roadmap L125(n)): the `ConstOffsets`
    // (plural) counterpart of `Gather2D` above -- 4 independent
    // `(offset_x, offset_y)` pairs, one per gathered corner, in place of
    // `Gather2D`'s own single shared pair.
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, I32Ty, I32Ty,
         I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherArray2DOffsets:
    // Same as Gather2DOffsets above, plus `array_layer` (`f32`) right
    // after `v`, mirroring `GatherArray2D`'s own relationship to
    // `Gather2D`.
    FTy = FunctionType::get(
        V4F32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, I32Ty,
         I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Gather2DOffsetsI32:
    // Same as Gather2DOffsets above, returning <4 x i32> instead of
    // <4 x float>, mirroring Gather2DI32's own relationship to
    // Gather2D.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, I32Ty, I32Ty,
         I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::GatherArray2DOffsetsI32:
    // Same as GatherArray2DOffsets above, returning <4 x i32> instead of
    // <4 x float>, mirroring GatherArray2DI32's own relationship to
    // GatherArray2D.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, I32Ty,
         I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample1DI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, lod, offset, mask) -> <4 x i32>
    // (roadmap L125(b)): the `Plain1D` counterpart of `Sample2DI32`'s own
    // operand list, minus `v` (a bare scalar `u` coordinate, mirroring
    // `Sample1D`'s own relationship to `Sample2D`) and with a single
    // scalar `offset` in place of `offset_x`/`offset_y`.
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, I32Ty, I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample1DArrayI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, array_layer, lod, offset, mask) ->
    // <4 x i32> (roadmap L125(b)): the `Array1D` counterpart of
    // `Sample1DI32`, adding `array_layer` alongside `u`, mirroring
    // `Sample1DArray`'s own relationship to `Sample1D`.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample2DArrayI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, array_layer, lod, offset_x,
    //  offset_y, mask) -> <4 x i32> (roadmap L125(b)): the `Array2D`
    // counterpart of `Sample2DI32`, adding `array_layer` alongside `u`/
    // `v`, mirroring `Sample2DArray`'s own relationship to `Sample2D`.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::Sample3DI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, u, v, w, lod, offset_x, offset_y,
    //  offset_z, mask) -> <4 x i32> (roadmap L125(b)): the `Plain3D`
    // counterpart of `Sample2DI32`, adding `w` alongside `u`/`v` and a
    // genuine third `offset_z`, mirroring `Sample3D`'s own relationship
    // to `Sample2D`.
    FTy = FunctionType::get(V4I32Ty,
                            {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty,
                             F32Ty, F32Ty, F32Ty, I32Ty, I32Ty, I32Ty, I1Ty},
                            /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCubeI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, lod, mask) ->
    // <4 x i32> (roadmap L125(b)): the `Cube` counterpart of
    // `Sample2DI32`, a 3-component direction vector in place of `u`/`v`
    // (mirroring `SampleCube`'s own relationship to `Sample2D`), and no
    // offset operand at all (SPIR-V forbids `ConstOffset` against
    // `Dim::Cube`).
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         I1Ty},
        /*isVarArg=*/false);
    break;
  case ImageCallKind::SampleCubeArrayI32:
    // (image_heap, image_heap_count, sampler_heap, sampler_heap_count,
    //  image_index, sampler_index, dir_x, dir_y, dir_z, array_layer,
    //  lod, mask) -> <4 x i32> (roadmap L125(b)): the `CubeArray`
    // counterpart of `SampleCubeI32`, adding a float `array_layer`
    // operand (mirroring `SampleCubeArray`'s own identical relationship
    // to `SampleCube`).
    FTy = FunctionType::get(
        V4I32Ty,
        {PtrTy, I32Ty, PtrTy, I32Ty, I32Ty, I32Ty, F32Ty, F32Ty, F32Ty, F32Ty,
         F32Ty, I1Ty},
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
                   Kind == ImageCallKind::Store3DI32 ||
                   Kind == ImageCallKind::Store1DArray ||
                   Kind == ImageCallKind::Store1DArrayI32 ||
                   Kind == ImageCallKind::Store2DMS ||
                   Kind == ImageCallKind::Store2DMSI32;
    // An atomic (roadmap H8v) both reads and writes through its heap
    // pointer, unlike every ordinary Load*/Store* kind above (each of
    // which only ever does one or the other) -- so it needs its own,
    // third `MemoryEffects` shape rather than reusing `IsStore`'s
    // Store-only or the default Load-only case.
    bool IsAtomic = Kind == ImageCallKind::AtomicAdd2D ||
                    Kind == ImageCallKind::AtomicSub2D ||
                    Kind == ImageCallKind::AtomicAnd2D ||
                    Kind == ImageCallKind::AtomicOr2D ||
                    Kind == ImageCallKind::AtomicXor2D ||
                    Kind == ImageCallKind::AtomicSMax2D ||
                    Kind == ImageCallKind::AtomicSMin2D ||
                    Kind == ImageCallKind::AtomicUMax2D ||
                    Kind == ImageCallKind::AtomicUMin2D ||
                    Kind == ImageCallKind::AtomicExchange2D ||
                    Kind == ImageCallKind::AtomicCompareExchange2D;
    F->setMemoryEffects(
        IsAtomic ? MemoryEffects::argMemOnly(ModRefInfo::ModRef)
                 : MemoryEffects::argMemOnly(IsStore ? ModRefInfo::Mod
                                                      : ModRefInfo::Ref));
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
                                    Value *UseExplicitLod, Value *Bias,
                                    Value *OffsetX, Value *OffsetY,
                                    Value *MinLodClamp, Value *Mask,
                                    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, DUdX, DUdY,
          DVdX, DVdY, Lod, UseExplicitLod, Bias, OffsetX, OffsetY,
          MinLodClamp, Mask},
      Name);
}

CallInst *feme::cpu::createSample2DI32(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *SamplerIndex,
                                       Value *U, Value *V, Value *Lod,
                                       Value *OffsetX, Value *OffsetY,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, Lod, OffsetX,
          OffsetY, Mask},
      Name);
}

CallInst *feme::cpu::createSample1DI32(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *SamplerIndex,
                                       Value *U, Value *Lod, Value *Offset,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample1DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, Lod, Offset,
          Mask},
      Name);
}

CallInst *feme::cpu::createSample1DArrayI32(IRBuilderBase &Builder,
                                            const ImageCallEnv &Env,
                                            Value *ImageIndex,
                                            Value *SamplerIndex, Value *U,
                                            Value *ArrayLayer, Value *Lod,
                                            Value *Offset, Value *Mask,
                                            const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample1DArrayI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, ArrayLayer, Lod,
          Offset, Mask},
      Name);
}

CallInst *feme::cpu::createSample2DArrayI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer, Value *Lod,
    Value *OffsetX, Value *OffsetY, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2DArrayI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, ArrayLayer,
          Lod, OffsetX, OffsetY, Mask},
      Name);
}

CallInst *feme::cpu::createSample3DI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *W, Value *Lod,
    Value *OffsetX, Value *OffsetY, Value *OffsetZ, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample3DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, W, Lod,
          OffsetX, OffsetY, OffsetZ, Mask},
      Name);
}

CallInst *feme::cpu::createSampleCubeI32(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex,
                                         Value *SamplerIndex, Value *DirX,
                                         Value *DirY, Value *DirZ, Value *Lod,
                                         Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCubeI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, DirX, DirY, DirZ,
          Lod, Mask},
      Name);
}

CallInst *feme::cpu::createSampleCubeArrayI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ,
    Value *ArrayLayer, Value *Lod, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCubeArrayI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, DirX, DirY, DirZ,
          ArrayLayer, Lod, Mask},
      Name);
}

CallInst *feme::cpu::createSampleCmp2D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *DUdX, Value *DUdY,
    Value *DVdX, Value *DVdY, Value *Lod, Value *UseExplicitLod, Value *Dref,
    Value *Bias, Value *OffsetX, Value *OffsetY, Value *MinLodClamp,
    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmp2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             DUdX,
                             DUdY,
                             DVdX,
                             DVdY,
                             Lod,
                             UseExplicitLod,
                             Dref,
                             Bias,
                             OffsetX,
                             OffsetY,
                             MinLodClamp,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGatherCmp2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *SamplerIndex,
                                       Value *U, Value *V, Value *Dref,
                                       Value *OffsetX, Value *OffsetY,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherCmp2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             Dref,
                             OffsetX,
                             OffsetY,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGather2D(IRBuilderBase &Builder,
                                    const ImageCallEnv &Env,
                                    Value *ImageIndex, Value *SamplerIndex,
                                    Value *U, Value *V, Value *Component,
                                    Value *OffsetX, Value *OffsetY,
                                    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Gather2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             Component,
                             OffsetX,
                             OffsetY,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGatherCmpArray2D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer, Value *Dref,
    Value *OffsetX, Value *OffsetY, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherCmpArray2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, ArrayLayer, Dref, OffsetX, OffsetY, Mask},
                            Name);
}

CallInst *feme::cpu::createGatherArray2D(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex, Value *SamplerIndex,
                                         Value *U, Value *V, Value *ArrayLayer,
                                         Value *Component, Value *OffsetX,
                                         Value *OffsetY, Value *Mask,
                                         const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherArray2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, ArrayLayer, Component, OffsetX, OffsetY, Mask},
                            Name);
}

CallInst *feme::cpu::createGatherCmpCube(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex, Value *SamplerIndex,
                                         Value *DirX, Value *DirY, Value *DirZ,
                                         Value *Dref, Value *Mask,
                                         const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherCmpCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, Dref, Mask},
                            Name);
}

CallInst *feme::cpu::createGatherCube(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *SamplerIndex,
                                      Value *DirX, Value *DirY, Value *DirZ,
                                      Value *Component, Value *Mask,
                                      const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, Component, Mask},
                            Name);
}

CallInst *feme::cpu::createGather2DI32(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *SamplerIndex,
                                       Value *U, Value *V, Value *Component,
                                       Value *OffsetX, Value *OffsetY,
                                       Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Gather2DI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             Component,
                             OffsetX,
                             OffsetY,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGatherArray2DI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer,
    Value *Component, Value *OffsetX, Value *OffsetY, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherArray2DI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, ArrayLayer, Component, OffsetX, OffsetY, Mask},
                            Name);
}

CallInst *feme::cpu::createGatherCubeI32(IRBuilderBase &Builder,
                                         const ImageCallEnv &Env,
                                         Value *ImageIndex, Value *SamplerIndex,
                                         Value *DirX, Value *DirY, Value *DirZ,
                                         Value *Component, Value *Mask,
                                         const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherCubeI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, Component, Mask},
                            Name);
}

CallInst *feme::cpu::createGather2DOffsets(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *Component,
    Value *OffsetX0, Value *OffsetY0, Value *OffsetX1, Value *OffsetY1,
    Value *OffsetX2, Value *OffsetY2, Value *OffsetX3, Value *OffsetY3,
    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Gather2DOffsets);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, Component, OffsetX0, OffsetY0, OffsetX1,
                             OffsetY1, OffsetX2, OffsetY2, OffsetX3, OffsetY3,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGatherArray2DOffsets(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer,
    Value *Component, Value *OffsetX0, Value *OffsetY0, Value *OffsetX1,
    Value *OffsetY1, Value *OffsetX2, Value *OffsetY2, Value *OffsetX3,
    Value *OffsetY3, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GatherArray2DOffsets);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
       Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, ArrayLayer,
       Component, OffsetX0, OffsetY0, OffsetX1, OffsetY1, OffsetX2, OffsetY2,
       OffsetX3, OffsetY3, Mask},
      Name);
}

CallInst *feme::cpu::createGather2DOffsetsI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *Component,
    Value *OffsetX0, Value *OffsetY0, Value *OffsetX1, Value *OffsetY1,
    Value *OffsetX2, Value *OffsetY2, Value *OffsetX3, Value *OffsetY3,
    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Gather2DOffsetsI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             V, Component, OffsetX0, OffsetY0, OffsetX1,
                             OffsetY1, OffsetX2, OffsetY2, OffsetX3, OffsetY3,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createGatherArray2DOffsetsI32(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer,
    Value *Component, Value *OffsetX0, Value *OffsetY0, Value *OffsetX1,
    Value *OffsetY1, Value *OffsetX2, Value *OffsetY2, Value *OffsetX3,
    Value *OffsetY3, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F =
      getOrInsertImageCall(*M, ImageCallKind::GatherArray2DOffsetsI32);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
       Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, ArrayLayer,
       Component, OffsetX0, OffsetY0, OffsetX1, OffsetY1, OffsetX2, OffsetY2,
       OffsetX3, OffsetY3, Mask},
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
                                     Value *Mip, Value *Sample, Value *Mask,
                                     const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2DI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Mip, Sample,
          Mask},
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

CallInst *feme::cpu::createStore2DMS(IRBuilderBase &Builder,
                                     const ImageCallEnv &Env,
                                     Value *ImageIndex, Value *X, Value *Y,
                                     Value *Sample, Value *Texel, Value *Mask,
                                     const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DMS);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Sample, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createStore2DMSI32(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X,
                                        Value *Y, Value *Sample, Value *Texel,
                                        Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DMSI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Sample, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createStore2DArrayMS(IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          Value *ImageIndex, Value *X,
                                          Value *Y, Value *Layer,
                                          Value *Sample, Value *Texel,
                                          Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DArrayMS);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Sample, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createStore2DArrayMSI32(IRBuilderBase &Builder,
                                             const ImageCallEnv &Env,
                                             Value *ImageIndex, Value *X,
                                             Value *Y, Value *Layer,
                                             Value *Sample, Value *Texel,
                                             Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store2DArrayMSI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Sample, Texel, Mask},
                            Name);
}

CallInst *feme::cpu::createSample2DArray(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer, Value *DUdX,
    Value *DUdY, Value *DVdX, Value *DVdY, Value *Lod, Value *UseExplicitLod,
    Value *Bias, Value *OffsetX, Value *OffsetY, Value *MinLodClamp,
    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample2DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             ArrayLayer,
                             DUdX,
                             DUdY,
                             DVdX,
                             DVdY,
                             Lod,
                             UseExplicitLod,
                             Bias,
                             OffsetX,
                             OffsetY,
                             MinLodClamp,
                             Mask},
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
                                         Value *Sample, Value *Mask,
                                         const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load2DArrayI32);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Layer, Mip, Sample, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCube(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ,
    Value *DDirXdX, Value *DDirXdY, Value *DDirYdX, Value *DDirYdY,
    Value *DDirZdX, Value *DDirZdY, Value *Lod, Value *UseExplicitLod,
    Value *Bias, Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, DDirXdX, DDirXdY, DDirYdX,
                             DDirYdY, DDirZdX, DDirZdY, Lod, UseExplicitLod,
                             Bias, MinLodClamp, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCubeArray(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ,
    Value *DDirXdX, Value *DDirXdY, Value *DDirYdX, Value *DDirYdY,
    Value *DDirZdX, Value *DDirZdY, Value *ArrayLayer, Value *Lod,
    Value *UseExplicitLod, Value *Bias, Value *MinLodClamp, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCubeArray);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
       Env.SamplerHeapCount, ImageIndex, SamplerIndex, DirX, DirY, DirZ,
       DDirXdX, DDirXdY, DDirYdX, DDirYdY, DDirZdX, DDirZdY, ArrayLayer, Lod,
       UseExplicitLod, Bias, MinLodClamp, Mask},
      Name);
}

CallInst *feme::cpu::createSampleCmpArray2D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *ArrayLayer, Value *DUdX,
    Value *DUdY, Value *DVdX, Value *DVdY, Value *Lod, Value *UseExplicitLod,
    Value *Dref, Value *Bias, Value *OffsetX, Value *OffsetY,
    Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmpArray2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             U,
                             V,
                             ArrayLayer,
                             DUdX,
                             DUdY,
                             DVdX,
                             DVdY,
                             Lod,
                             UseExplicitLod,
                             Dref,
                             Bias,
                             OffsetX,
                             OffsetY,
                             MinLodClamp,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCmpCube(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ, Value *DDirXdX,
    Value *DDirXdY, Value *DDirYdX, Value *DDirYdY, Value *DDirZdX,
    Value *DDirZdY, Value *Lod, Value *UseExplicitLod, Value *Dref, Value *Bias,
    Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmpCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             DirX,
                             DirY,
                             DirZ,
                             DDirXdX,
                             DDirXdY,
                             DDirYdX,
                             DDirYdY,
                             DDirZdX,
                             DDirZdY,
                             Lod,
                             UseExplicitLod,
                             Dref,
                             Bias,
                             MinLodClamp,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCmpCubeArray(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ, Value *DDirXdX,
    Value *DDirXdY, Value *DDirYdX, Value *DDirYdY, Value *DDirZdX,
    Value *DDirZdY, Value *ArrayLayer, Value *Lod, Value *UseExplicitLod,
    Value *Dref, Value *Bias, Value *MinLodClamp, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmpCubeArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap,
                             Env.ImageHeapCount,
                             Env.SamplerHeap,
                             Env.SamplerHeapCount,
                             ImageIndex,
                             SamplerIndex,
                             DirX,
                             DirY,
                             DirZ,
                             DDirXdX,
                             DDirXdY,
                             DDirYdX,
                             DDirYdY,
                             DDirZdX,
                             DDirZdY,
                             ArrayLayer,
                             Lod,
                             UseExplicitLod,
                             Dref,
                             Bias,
                             MinLodClamp,
                             Mask},
                            Name);
}

CallInst *feme::cpu::createSample1D(IRBuilderBase &Builder,
                                    const ImageCallEnv &Env, Value *ImageIndex,
                                    Value *SamplerIndex, Value *U, Value *DUdX,
                                    Value *DUdY, Value *Lod,
                                    Value *UseExplicitLod, Value *Bias,
                                    Value *Offset, Value *MinLodClamp,
                                    Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample1D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             DUdX, DUdY, Lod, UseExplicitLod, Bias, Offset,
                             MinLodClamp, Mask},
                            Name);
}

CallInst *feme::cpu::createSample1DArray(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *ArrayLayer, Value *DUdX, Value *DUdY,
    Value *Lod, Value *UseExplicitLod, Value *Bias, Value *Offset,
    Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample1DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             ArrayLayer, DUdX, DUdY, Lod, UseExplicitLod, Bias,
                             Offset, MinLodClamp, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCmp1D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *DUdX, Value *DUdY, Value *Lod,
    Value *UseExplicitLod, Value *Dref, Value *Bias, Value *Offset,
    Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmp1D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             DUdX, DUdY, Lod, UseExplicitLod, Dref, Bias,
                             Offset, MinLodClamp, Mask},
                            Name);
}

CallInst *feme::cpu::createSampleCmpArray1D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *ArrayLayer, Value *DUdX, Value *DUdY,
    Value *Lod, Value *UseExplicitLod, Value *Dref, Value *Bias, Value *Offset,
    Value *MinLodClamp, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::SampleCmpArray1D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex, U,
                             ArrayLayer, DUdX, DUdY, Lod, UseExplicitLod, Dref,
                             Bias, Offset, MinLodClamp, Mask},
                            Name);
}

CallInst *feme::cpu::createQueryLod2D(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *SamplerIndex,
                                      Value *DUdX, Value *DUdY, Value *DVdX,
                                      Value *DVdY, Value *Mask,
                                      const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QueryLod2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, DUdX, DUdY, DVdX,
          DVdY, Mask},
      Name);
}

CallInst *feme::cpu::createQueryLodCube(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *DirX, Value *DirY, Value *DirZ, Value *DDirXdX,
    Value *DDirXdY, Value *DDirYdX, Value *DDirYdY, Value *DDirZdX,
    Value *DDirZdY, Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QueryLodCube);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
                             Env.SamplerHeapCount, ImageIndex, SamplerIndex,
                             DirX, DirY, DirZ, DDirXdX, DDirXdY, DDirYdX,
                             DDirYdY, DDirZdX, DDirZdY, Mask},
                            Name);
}

CallInst *feme::cpu::createSample3D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *SamplerIndex, Value *U, Value *V, Value *W, Value *DUdX,
    Value *DUdY, Value *DVdX, Value *DVdY, Value *DWdX, Value *DWdY,
    Value *Lod, Value *UseExplicitLod, Value *Bias, Value *OffsetX,
    Value *OffsetY, Value *OffsetZ, Value *MinLodClamp, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Sample3D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, Env.SamplerHeap,
          Env.SamplerHeapCount, ImageIndex, SamplerIndex, U, V, W, DUdX, DUdY,
          DVdX, DVdY, DWdX, DWdY, Lod, UseExplicitLod, Bias, OffsetX, OffsetY,
          OffsetZ, MinLodClamp, Mask},
      Name);
}

CallInst *feme::cpu::createGetDimensions2D(IRBuilderBase &Builder,
                                           const ImageCallEnv &Env,
                                           Value *ImageIndex, Value *Mask,
                                           const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::GetDimensions2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLod2D(IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          Value *ImageIndex, Value *Lod,
                                          Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLod2D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLod1D(IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          Value *ImageIndex, Value *Lod,
                                          Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLod1D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLod1DArray(IRBuilderBase &Builder,
                                               const ImageCallEnv &Env,
                                               Value *ImageIndex, Value *Lod,
                                               Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLod1DArray);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLod2DArray(IRBuilderBase &Builder,
                                               const ImageCallEnv &Env,
                                               Value *ImageIndex, Value *Lod,
                                               Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLod2DArray);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLod3D(IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          Value *ImageIndex, Value *Lod,
                                          Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLod3D);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQuerySizeLodCubeArray(IRBuilderBase &Builder,
                                                 const ImageCallEnv &Env,
                                                 Value *ImageIndex, Value *Lod,
                                                 Value *Mask,
                                                 const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySizeLodCubeArray);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, Lod, Mask}, Name);
}

CallInst *feme::cpu::createQueryLevels(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QueryLevels);
  return Builder.CreateCall(F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex},
                            Name);
}

CallInst *feme::cpu::createQuerySamples(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::QuerySamples);
  return Builder.CreateCall(F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex},
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

CallInst *feme::cpu::createLoad1DArray(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X,
                                       Value *Layer, Value *Mip,
                                       Value *Sample, Value *Mask,
                                       const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load1DArray);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Layer, Mip, Sample, Mask},
                            Name);
}

CallInst *feme::cpu::createLoad1DArrayI32(IRBuilderBase &Builder,
                                          const ImageCallEnv &Env,
                                          Value *ImageIndex, Value *X,
                                          Value *Layer, Value *Mip,
                                          Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Load1DArrayI32);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Layer, Mip, Mask},
      Name);
}

CallInst *feme::cpu::createStore1DArray(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X,
                                        Value *Layer, Value *Texel,
                                        Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store1DArray);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Layer, Texel, Mask},
      Name);
}

CallInst *feme::cpu::createStore1DArrayI32(IRBuilderBase &Builder,
                                           const ImageCallEnv &Env,
                                           Value *ImageIndex, Value *X,
                                           Value *Layer, Value *Texel,
                                           Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, ImageCallKind::Store1DArrayI32);
  return Builder.CreateCall(
      F,
      {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Layer, Texel, Mask},
      Name);
}

// Shared by every createAtomic*2D wrapper below (roadmap H8v): builds the
// common (image_heap, image_heap_count, image_index, x, y, value, mask)
// call for whichever RMW \p Kind names.
static CallInst *createAtomicRMW2D(IRBuilderBase &Builder, ImageCallKind Kind,
                                   const ImageCallEnv &Env, Value *ImageIndex,
                                   Value *X, Value *Y, Value *Value_,
                                   Value *Mask, const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F = getOrInsertImageCall(*M, Kind);
  return Builder.CreateCall(
      F, {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X, Y, Value_, Mask},
      Name);
}

CallInst *feme::cpu::createAtomicAdd2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X, Value *Y,
                                       Value *Value_, Value *Mask,
                                       const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicAdd2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicSub2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X, Value *Y,
                                       Value *Value_, Value *Mask,
                                       const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicSub2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicAnd2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X, Value *Y,
                                       Value *Value_, Value *Mask,
                                       const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicAnd2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicOr2D(IRBuilderBase &Builder,
                                      const ImageCallEnv &Env,
                                      Value *ImageIndex, Value *X, Value *Y,
                                      Value *Value_, Value *Mask,
                                      const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicOr2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicXor2D(IRBuilderBase &Builder,
                                       const ImageCallEnv &Env,
                                       Value *ImageIndex, Value *X, Value *Y,
                                       Value *Value_, Value *Mask,
                                       const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicXor2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicSMax2D(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X, Value *Y,
                                        Value *Value_, Value *Mask,
                                        const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicSMax2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicSMin2D(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X, Value *Y,
                                        Value *Value_, Value *Mask,
                                        const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicSMin2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicUMax2D(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X, Value *Y,
                                        Value *Value_, Value *Mask,
                                        const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicUMax2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicUMin2D(IRBuilderBase &Builder,
                                        const ImageCallEnv &Env,
                                        Value *ImageIndex, Value *X, Value *Y,
                                        Value *Value_, Value *Mask,
                                        const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicUMin2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicExchange2D(IRBuilderBase &Builder,
                                            const ImageCallEnv &Env,
                                            Value *ImageIndex, Value *X,
                                            Value *Y, Value *Value_,
                                            Value *Mask, const Twine &Name) {
  return createAtomicRMW2D(Builder, ImageCallKind::AtomicExchange2D, Env,
                           ImageIndex, X, Y, Value_, Mask, Name);
}

CallInst *feme::cpu::createAtomicCompareExchange2D(
    IRBuilderBase &Builder, const ImageCallEnv &Env, Value *ImageIndex,
    Value *X, Value *Y, Value *Comparator, Value *Value_, Value *Mask,
    const Twine &Name) {
  Module *M = Builder.GetInsertBlock()->getModule();
  Function *F =
      getOrInsertImageCall(*M, ImageCallKind::AtomicCompareExchange2D);
  return Builder.CreateCall(F,
                            {Env.ImageHeap, Env.ImageHeapCount, ImageIndex, X,
                             Y, Comparator, Value_, Mask},
                            Name);
}

std::optional<MatchedImageCall> feme::cpu::matchImageCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  // NOTE: every ImageCallKind must be listed here or matchImageCall will
  // silently return std::nullopt for it, leaving its operand-extraction case
  // in the switch below unreachable dead code (see roadmap H19l: this table
  // omitted Store2DMS/Store2DMSI32 for a long time without being caught,
  // since nothing enforces that this array and the enum/switch stay in
  // sync). Keep new ImageCallKind values added here whenever they are added
  // to the enum and to the switch below.
  static constexpr ImageCallKind AllKinds[] = {
      ImageCallKind::Sample2D,
      ImageCallKind::SampleCmp2D,
      ImageCallKind::Load2D,
      ImageCallKind::Load2DI32,
      ImageCallKind::Sample2DArray,
      ImageCallKind::Load2DArray,
      ImageCallKind::Load2DArrayI32,
      ImageCallKind::SampleCube,
      ImageCallKind::SampleCubeArray,
      ImageCallKind::Store2D,
      ImageCallKind::Store2DI32,
      ImageCallKind::Store2DArray,
      ImageCallKind::Store2DArrayI32,
      ImageCallKind::Load1D,
      ImageCallKind::Load1DI32,
      ImageCallKind::Store1D,
      ImageCallKind::Store1DI32,
      ImageCallKind::Load3D,
      ImageCallKind::Load3DI32,
      ImageCallKind::Store3D,
      ImageCallKind::Store3DI32,
      ImageCallKind::Load1DArray,
      ImageCallKind::Load1DArrayI32,
      ImageCallKind::Store1DArray,
      ImageCallKind::Store1DArrayI32,
      ImageCallKind::Store2DMS,
      ImageCallKind::Store2DMSI32,
      ImageCallKind::Store2DArrayMS,
      ImageCallKind::Store2DArrayMSI32,
      ImageCallKind::AtomicAdd2D,
      ImageCallKind::AtomicSub2D,
      ImageCallKind::AtomicAnd2D,
      ImageCallKind::AtomicOr2D,
      ImageCallKind::AtomicXor2D,
      ImageCallKind::AtomicSMax2D,
      ImageCallKind::AtomicSMin2D,
      ImageCallKind::AtomicUMax2D,
      ImageCallKind::AtomicUMin2D,
      ImageCallKind::AtomicExchange2D,
      ImageCallKind::AtomicCompareExchange2D,
      ImageCallKind::SampleCmpArray2D,
      ImageCallKind::SampleCmpCube,
      ImageCallKind::SampleCmpCubeArray,
      ImageCallKind::Sample1D,
      ImageCallKind::Sample1DArray,
      ImageCallKind::SampleCmp1D,
      ImageCallKind::SampleCmpArray1D,
      ImageCallKind::QueryLod2D,
      ImageCallKind::QueryLodCube,
      ImageCallKind::Sample3D,
      ImageCallKind::GetDimensions2D,
      ImageCallKind::QuerySizeLod2D,
      ImageCallKind::QuerySizeLod1D,
      ImageCallKind::QuerySizeLod1DArray,
      ImageCallKind::QuerySizeLod2DArray,
      ImageCallKind::QuerySizeLod3D,
      ImageCallKind::QuerySizeLodCubeArray,
      ImageCallKind::QueryLevels,
      ImageCallKind::QuerySamples,
      ImageCallKind::GatherCmp2D,
      ImageCallKind::Gather2D,
      ImageCallKind::GatherCmpArray2D,
      ImageCallKind::GatherArray2D,
      ImageCallKind::Sample2DI32,
      ImageCallKind::GatherCmpCube,
      ImageCallKind::GatherCube,
      ImageCallKind::Gather2DI32,
      ImageCallKind::GatherArray2DI32,
      ImageCallKind::GatherCubeI32,
      ImageCallKind::Gather2DOffsets,
      ImageCallKind::GatherArray2DOffsets,
      ImageCallKind::Gather2DOffsetsI32,
      ImageCallKind::GatherArray2DOffsetsI32,
      ImageCallKind::Sample1DI32,
      ImageCallKind::Sample1DArrayI32,
      ImageCallKind::Sample2DArrayI32,
      ImageCallKind::Sample3DI32,
      ImageCallKind::SampleCubeI32,
      ImageCallKind::SampleCubeArrayI32};

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
    if (CI.arg_size() != 19)
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
    Result.Bias = CI.getArgOperand(14);
    Result.OffsetX = CI.getArgOperand(15);
    Result.OffsetY = CI.getArgOperand(16);
    Result.MinLodClamp = CI.getArgOperand(17);
    Result.Mask = CI.getArgOperand(18);
    break;
  case ImageCallKind::SampleCmp2D:
    if (CI.arg_size() != 20)
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
    Result.Dref = CI.getArgOperand(14);
    Result.Bias = CI.getArgOperand(15);
    Result.OffsetX = CI.getArgOperand(16);
    Result.OffsetY = CI.getArgOperand(17);
    Result.MinLodClamp = CI.getArgOperand(18);
    Result.Mask = CI.getArgOperand(19);
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
  case ImageCallKind::Sample2DArray:
    if (CI.arg_size() != 20)
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
    Result.DUdX = CI.getArgOperand(9);
    Result.DUdY = CI.getArgOperand(10);
    Result.DVdX = CI.getArgOperand(11);
    Result.DVdY = CI.getArgOperand(12);
    Result.Lod = CI.getArgOperand(13);
    Result.UseExplicitLod = CI.getArgOperand(14);
    Result.Bias = CI.getArgOperand(15);
    Result.OffsetX = CI.getArgOperand(16);
    Result.OffsetY = CI.getArgOperand(17);
    Result.MinLodClamp = CI.getArgOperand(18);
    Result.Mask = CI.getArgOperand(19);
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
  case ImageCallKind::SampleCube:
    if (CI.arg_size() != 20)
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
    Result.DDirXdX = CI.getArgOperand(9);
    Result.DDirXdY = CI.getArgOperand(10);
    Result.DDirYdX = CI.getArgOperand(11);
    Result.DDirYdY = CI.getArgOperand(12);
    Result.DDirZdX = CI.getArgOperand(13);
    Result.DDirZdY = CI.getArgOperand(14);
    Result.Lod = CI.getArgOperand(15);
    Result.UseExplicitLod = CI.getArgOperand(16);
    Result.Bias = CI.getArgOperand(17);
    Result.MinLodClamp = CI.getArgOperand(18);
    Result.Mask = CI.getArgOperand(19);
    break;
  case ImageCallKind::SampleCubeArray:
    if (CI.arg_size() != 21)
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
    Result.DDirXdX = CI.getArgOperand(9);
    Result.DDirXdY = CI.getArgOperand(10);
    Result.DDirYdX = CI.getArgOperand(11);
    Result.DDirYdY = CI.getArgOperand(12);
    Result.DDirZdX = CI.getArgOperand(13);
    Result.DDirZdY = CI.getArgOperand(14);
    Result.ArrayLayer = CI.getArgOperand(15);
    Result.Lod = CI.getArgOperand(16);
    Result.UseExplicitLod = CI.getArgOperand(17);
    Result.Bias = CI.getArgOperand(18);
    Result.MinLodClamp = CI.getArgOperand(19);
    Result.Mask = CI.getArgOperand(20);
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
  case ImageCallKind::Load1DArray:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Layer = CI.getArgOperand(4);
    Result.Lod = CI.getArgOperand(5);
    Result.Sample = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::Load1DArrayI32:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Layer = CI.getArgOperand(4);
    Result.Lod = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  case ImageCallKind::Store1DArray:
  case ImageCallKind::Store1DArrayI32:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.Layer = CI.getArgOperand(4);
    Result.Texel = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  case ImageCallKind::Store2DMS:
  case ImageCallKind::Store2DMSI32:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Sample = CI.getArgOperand(5);
    Result.Texel = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::Store2DArrayMS:
  case ImageCallKind::Store2DArrayMSI32:
    if (CI.arg_size() != 9)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Layer = CI.getArgOperand(5);
    Result.Sample = CI.getArgOperand(6);
    Result.Texel = CI.getArgOperand(7);
    Result.Mask = CI.getArgOperand(8);
    break;
  case ImageCallKind::AtomicAdd2D:
  case ImageCallKind::AtomicSub2D:
  case ImageCallKind::AtomicAnd2D:
  case ImageCallKind::AtomicOr2D:
  case ImageCallKind::AtomicXor2D:
  case ImageCallKind::AtomicSMax2D:
  case ImageCallKind::AtomicSMin2D:
  case ImageCallKind::AtomicUMax2D:
  case ImageCallKind::AtomicUMin2D:
  case ImageCallKind::AtomicExchange2D:
    if (CI.arg_size() != 7)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.AtomicValue = CI.getArgOperand(5);
    Result.Mask = CI.getArgOperand(6);
    break;
  case ImageCallKind::AtomicCompareExchange2D:
    if (CI.arg_size() != 8)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.U = CI.getArgOperand(3);
    Result.V = CI.getArgOperand(4);
    Result.Comparator = CI.getArgOperand(5);
    Result.AtomicValue = CI.getArgOperand(6);
    Result.Mask = CI.getArgOperand(7);
    break;
  case ImageCallKind::SampleCmpArray2D:
    if (CI.arg_size() != 21)
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
    Result.DUdX = CI.getArgOperand(9);
    Result.DUdY = CI.getArgOperand(10);
    Result.DVdX = CI.getArgOperand(11);
    Result.DVdY = CI.getArgOperand(12);
    Result.Lod = CI.getArgOperand(13);
    Result.UseExplicitLod = CI.getArgOperand(14);
    Result.Dref = CI.getArgOperand(15);
    Result.Bias = CI.getArgOperand(16);
    Result.OffsetX = CI.getArgOperand(17);
    Result.OffsetY = CI.getArgOperand(18);
    Result.MinLodClamp = CI.getArgOperand(19);
    Result.Mask = CI.getArgOperand(20);
    break;
  case ImageCallKind::SampleCmpCube:
    if (CI.arg_size() != 21)
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
    Result.DDirXdX = CI.getArgOperand(9);
    Result.DDirXdY = CI.getArgOperand(10);
    Result.DDirYdX = CI.getArgOperand(11);
    Result.DDirYdY = CI.getArgOperand(12);
    Result.DDirZdX = CI.getArgOperand(13);
    Result.DDirZdY = CI.getArgOperand(14);
    Result.Lod = CI.getArgOperand(15);
    Result.UseExplicitLod = CI.getArgOperand(16);
    Result.Dref = CI.getArgOperand(17);
    Result.Bias = CI.getArgOperand(18);
    Result.MinLodClamp = CI.getArgOperand(19);
    Result.Mask = CI.getArgOperand(20);
    break;
  case ImageCallKind::SampleCmpCubeArray:
    if (CI.arg_size() != 22)
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
    Result.DDirXdX = CI.getArgOperand(9);
    Result.DDirXdY = CI.getArgOperand(10);
    Result.DDirYdX = CI.getArgOperand(11);
    Result.DDirYdY = CI.getArgOperand(12);
    Result.DDirZdX = CI.getArgOperand(13);
    Result.DDirZdY = CI.getArgOperand(14);
    Result.ArrayLayer = CI.getArgOperand(15);
    Result.Lod = CI.getArgOperand(16);
    Result.UseExplicitLod = CI.getArgOperand(17);
    Result.Dref = CI.getArgOperand(18);
    Result.Bias = CI.getArgOperand(19);
    Result.MinLodClamp = CI.getArgOperand(20);
    Result.Mask = CI.getArgOperand(21);
    break;
  case ImageCallKind::Sample1D:
    if (CI.arg_size() != 15)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.DUdX = CI.getArgOperand(7);
    Result.DUdY = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.UseExplicitLod = CI.getArgOperand(10);
    Result.Bias = CI.getArgOperand(11);
    Result.OffsetX = CI.getArgOperand(12);
    Result.MinLodClamp = CI.getArgOperand(13);
    Result.Mask = CI.getArgOperand(14);
    break;
  case ImageCallKind::Sample1DArray:
    if (CI.arg_size() != 16)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.ArrayLayer = CI.getArgOperand(7);
    Result.DUdX = CI.getArgOperand(8);
    Result.DUdY = CI.getArgOperand(9);
    Result.Lod = CI.getArgOperand(10);
    Result.UseExplicitLod = CI.getArgOperand(11);
    Result.Bias = CI.getArgOperand(12);
    Result.OffsetX = CI.getArgOperand(13);
    Result.MinLodClamp = CI.getArgOperand(14);
    Result.Mask = CI.getArgOperand(15);
    break;
  case ImageCallKind::SampleCmp1D:
    if (CI.arg_size() != 16)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.DUdX = CI.getArgOperand(7);
    Result.DUdY = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.UseExplicitLod = CI.getArgOperand(10);
    Result.Dref = CI.getArgOperand(11);
    Result.Bias = CI.getArgOperand(12);
    Result.OffsetX = CI.getArgOperand(13);
    Result.MinLodClamp = CI.getArgOperand(14);
    Result.Mask = CI.getArgOperand(15);
    break;
  case ImageCallKind::SampleCmpArray1D:
    if (CI.arg_size() != 17)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.ArrayLayer = CI.getArgOperand(7);
    Result.DUdX = CI.getArgOperand(8);
    Result.DUdY = CI.getArgOperand(9);
    Result.Lod = CI.getArgOperand(10);
    Result.UseExplicitLod = CI.getArgOperand(11);
    Result.Dref = CI.getArgOperand(12);
    Result.Bias = CI.getArgOperand(13);
    Result.OffsetX = CI.getArgOperand(14);
    Result.MinLodClamp = CI.getArgOperand(15);
    Result.Mask = CI.getArgOperand(16);
    break;
  case ImageCallKind::QueryLod2D:
    if (CI.arg_size() != 11)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.DUdX = CI.getArgOperand(6);
    Result.DUdY = CI.getArgOperand(7);
    Result.DVdX = CI.getArgOperand(8);
    Result.DVdY = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::QueryLodCube:
    if (CI.arg_size() != 16)
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
    Result.DDirXdX = CI.getArgOperand(9);
    Result.DDirXdY = CI.getArgOperand(10);
    Result.DDirYdX = CI.getArgOperand(11);
    Result.DDirYdY = CI.getArgOperand(12);
    Result.DDirZdX = CI.getArgOperand(13);
    Result.DDirZdY = CI.getArgOperand(14);
    Result.Mask = CI.getArgOperand(15);
    break;
  case ImageCallKind::Sample3D:
    if (CI.arg_size() != 23)
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
    Result.DUdX = CI.getArgOperand(9);
    Result.DUdY = CI.getArgOperand(10);
    Result.DVdX = CI.getArgOperand(11);
    Result.DVdY = CI.getArgOperand(12);
    Result.DWdX = CI.getArgOperand(13);
    Result.DWdY = CI.getArgOperand(14);
    Result.Lod = CI.getArgOperand(15);
    Result.UseExplicitLod = CI.getArgOperand(16);
    Result.Bias = CI.getArgOperand(17);
    Result.OffsetX = CI.getArgOperand(18);
    Result.OffsetY = CI.getArgOperand(19);
    Result.OffsetZ = CI.getArgOperand(20);
    Result.MinLodClamp = CI.getArgOperand(21);
    Result.Mask = CI.getArgOperand(22);
    break;
  case ImageCallKind::GetDimensions2D:
    if (CI.arg_size() != 4)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.Mask = CI.getArgOperand(3);
    break;
  case ImageCallKind::QuerySizeLod2D:
    if (CI.arg_size() != 5)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.Lod = CI.getArgOperand(3);
    Result.Mask = CI.getArgOperand(4);
    break;
  case ImageCallKind::QuerySizeLod1D:
  case ImageCallKind::QuerySizeLod1DArray:
  case ImageCallKind::QuerySizeLod2DArray:
  case ImageCallKind::QuerySizeLod3D:
  case ImageCallKind::QuerySizeLodCubeArray:
    // Roadmap L75: every other-shape `QuerySizeLod*` builder shares
    // `QuerySizeLod2D`'s own identical operand list above -- only the
    // result type (encoded in the callee's own declared return type, not
    // in `MatchedImageCall`) differs per shape.
    if (CI.arg_size() != 5)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    Result.Lod = CI.getArgOperand(3);
    Result.Mask = CI.getArgOperand(4);
    break;
  case ImageCallKind::QueryLevels:
    if (CI.arg_size() != 3)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    break;
  case ImageCallKind::QuerySamples:
    if (CI.arg_size() != 3)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.ImageIndex = CI.getArgOperand(2);
    break;
  case ImageCallKind::GatherCmp2D:
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
    Result.Dref = CI.getArgOperand(8);
    Result.OffsetX = CI.getArgOperand(9);
    Result.OffsetY = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::Gather2D:
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
    Result.Component = CI.getArgOperand(8);
    Result.OffsetX = CI.getArgOperand(9);
    Result.OffsetY = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::GatherCmpArray2D:
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
    Result.ArrayLayer = CI.getArgOperand(8);
    Result.Dref = CI.getArgOperand(9);
    Result.OffsetX = CI.getArgOperand(10);
    Result.OffsetY = CI.getArgOperand(11);
    Result.Mask = CI.getArgOperand(12);
    break;
  case ImageCallKind::GatherArray2D:
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
    Result.ArrayLayer = CI.getArgOperand(8);
    Result.Component = CI.getArgOperand(9);
    Result.OffsetX = CI.getArgOperand(10);
    Result.OffsetY = CI.getArgOperand(11);
    Result.Mask = CI.getArgOperand(12);
    break;
  case ImageCallKind::Sample2DI32:
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
    Result.OffsetX = CI.getArgOperand(9);
    Result.OffsetY = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::GatherCmpCube:
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
    Result.W = CI.getArgOperand(8);
    Result.Dref = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::GatherCube:
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
    Result.W = CI.getArgOperand(8);
    Result.Component = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::Gather2DI32:
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
    Result.Component = CI.getArgOperand(8);
    Result.OffsetX = CI.getArgOperand(9);
    Result.OffsetY = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
    break;
  case ImageCallKind::GatherArray2DI32:
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
    Result.ArrayLayer = CI.getArgOperand(8);
    Result.Component = CI.getArgOperand(9);
    Result.OffsetX = CI.getArgOperand(10);
    Result.OffsetY = CI.getArgOperand(11);
    Result.Mask = CI.getArgOperand(12);
    break;
  case ImageCallKind::GatherCubeI32:
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
    Result.W = CI.getArgOperand(8);
    Result.Component = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::Gather2DOffsets:
    if (CI.arg_size() != 18)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.Component = CI.getArgOperand(8);
    Result.OffsetX0 = CI.getArgOperand(9);
    Result.OffsetY0 = CI.getArgOperand(10);
    Result.OffsetX1 = CI.getArgOperand(11);
    Result.OffsetY1 = CI.getArgOperand(12);
    Result.OffsetX2 = CI.getArgOperand(13);
    Result.OffsetY2 = CI.getArgOperand(14);
    Result.OffsetX3 = CI.getArgOperand(15);
    Result.OffsetY3 = CI.getArgOperand(16);
    Result.Mask = CI.getArgOperand(17);
    break;
  case ImageCallKind::GatherArray2DOffsets:
    if (CI.arg_size() != 19)
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
    Result.Component = CI.getArgOperand(9);
    Result.OffsetX0 = CI.getArgOperand(10);
    Result.OffsetY0 = CI.getArgOperand(11);
    Result.OffsetX1 = CI.getArgOperand(12);
    Result.OffsetY1 = CI.getArgOperand(13);
    Result.OffsetX2 = CI.getArgOperand(14);
    Result.OffsetY2 = CI.getArgOperand(15);
    Result.OffsetX3 = CI.getArgOperand(16);
    Result.OffsetY3 = CI.getArgOperand(17);
    Result.Mask = CI.getArgOperand(18);
    break;
  case ImageCallKind::Gather2DOffsetsI32:
    if (CI.arg_size() != 18)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.V = CI.getArgOperand(7);
    Result.Component = CI.getArgOperand(8);
    Result.OffsetX0 = CI.getArgOperand(9);
    Result.OffsetY0 = CI.getArgOperand(10);
    Result.OffsetX1 = CI.getArgOperand(11);
    Result.OffsetY1 = CI.getArgOperand(12);
    Result.OffsetX2 = CI.getArgOperand(13);
    Result.OffsetY2 = CI.getArgOperand(14);
    Result.OffsetX3 = CI.getArgOperand(15);
    Result.OffsetY3 = CI.getArgOperand(16);
    Result.Mask = CI.getArgOperand(17);
    break;
  case ImageCallKind::GatherArray2DOffsetsI32:
    if (CI.arg_size() != 19)
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
    Result.Component = CI.getArgOperand(9);
    Result.OffsetX0 = CI.getArgOperand(10);
    Result.OffsetY0 = CI.getArgOperand(11);
    Result.OffsetX1 = CI.getArgOperand(12);
    Result.OffsetY1 = CI.getArgOperand(13);
    Result.OffsetX2 = CI.getArgOperand(14);
    Result.OffsetY2 = CI.getArgOperand(15);
    Result.OffsetX3 = CI.getArgOperand(16);
    Result.OffsetY3 = CI.getArgOperand(17);
    Result.Mask = CI.getArgOperand(18);
    break;
  case ImageCallKind::Sample1DI32:
    if (CI.arg_size() != 10)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.Lod = CI.getArgOperand(7);
    Result.OffsetX = CI.getArgOperand(8);
    Result.Mask = CI.getArgOperand(9);
    break;
  case ImageCallKind::Sample1DArrayI32:
    if (CI.arg_size() != 11)
      return std::nullopt;
    Result.Env.ImageHeap = CI.getArgOperand(0);
    Result.Env.ImageHeapCount = CI.getArgOperand(1);
    Result.Env.SamplerHeap = CI.getArgOperand(2);
    Result.Env.SamplerHeapCount = CI.getArgOperand(3);
    Result.ImageIndex = CI.getArgOperand(4);
    Result.SamplerIndex = CI.getArgOperand(5);
    Result.U = CI.getArgOperand(6);
    Result.ArrayLayer = CI.getArgOperand(7);
    Result.Lod = CI.getArgOperand(8);
    Result.OffsetX = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::Sample2DArrayI32:
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
    Result.ArrayLayer = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.OffsetX = CI.getArgOperand(10);
    Result.OffsetY = CI.getArgOperand(11);
    Result.Mask = CI.getArgOperand(12);
    break;
  case ImageCallKind::Sample3DI32:
    if (CI.arg_size() != 14)
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
    Result.OffsetX = CI.getArgOperand(10);
    Result.OffsetY = CI.getArgOperand(11);
    Result.OffsetZ = CI.getArgOperand(12);
    Result.Mask = CI.getArgOperand(13);
    break;
  case ImageCallKind::SampleCubeI32:
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
    Result.W = CI.getArgOperand(8);
    Result.Lod = CI.getArgOperand(9);
    Result.Mask = CI.getArgOperand(10);
    break;
  case ImageCallKind::SampleCubeArrayI32:
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
    Result.ArrayLayer = CI.getArgOperand(9);
    Result.Lod = CI.getArgOperand(10);
    Result.Mask = CI.getArgOperand(11);
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

SampleDerivatives1D
feme::cpu::getOrSynthesizeSample1DDerivatives(IRBuilderBase &B,
                                              Function &Caller, Value *U) {
  std::optional<feme::ShaderStage> Stage = feme::getShaderStage(Caller);
  if (Stage != feme::ShaderStage::Fragment) {
    Value *Zero = ConstantFP::get(B.getFloatTy(), 0.0);
    return {Zero, Zero};
  }
  return {feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      U),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      U)};
}

CubeDirectionDerivatives feme::cpu::getOrSynthesizeSampleCubeDerivatives(
    IRBuilderBase &B, Function &Caller, Value *DirX, Value *DirY,
    Value *DirZ) {
  std::optional<feme::ShaderStage> Stage = feme::getShaderStage(Caller);
  if (Stage != feme::ShaderStage::Fragment) {
    Value *Zero = ConstantFP::get(B.getFloatTy(), 0.0);
    return {Zero, Zero, Zero, Zero, Zero, Zero};
  }
  return {feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      DirX),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      DirX),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      DirY),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      DirY),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeXCoarse,
                                      DirZ),
          feme::createStageDerivative(B, feme::StageOpKind::DerivativeYCoarse,
                                      DirZ)};
}

