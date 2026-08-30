; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s

; Covers feme::cpu::ResourceLoweringPass's canonicalization of a bindless
; 2D texture sample (roadmap R30, "Canonical image operations" in
; feme/docs/FeMeGraphicsDesign.md): `llvm.dx.resource.sample`, consuming a
; `dx.Texture`/`dx.Sampler` pair from `llvm.dx.resource.handlefromheap`,
; becomes a single `feme.cpu.image.sample.2d.v4f32` call, and the rewritten
; function gains the same eight trailing ABI parameters
; resource-lowering-typed-buffer.ll documents (image/sampler-only functions
; still get all eight, not just `image_heap`/`image_heap_count` -- see
; `addResourceEnvParams`). Since neither function below carries a
; `feme.shader.stage`="fragment" attribute, roadmap H7i's four new
; screen-space-derivative operands (inserted between the `v` coordinate and
; `lod`) are always the zero constants `getOrSynthesizeSample2DDerivatives`
; falls back to outside the Fragment stage.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; A nonzero texel offset is left unraised: `runtime/CPU`'s helpers don't
; accept one yet (see `isZeroOffset`'s comment in ResourceLowering.cpp).
; The function's signature is therefore left unchanged too -- no ABI
; parameters are appended for an access that ends up not being rewritten.
; Placed first: `ResourceLoweringPass` appends every *rewritten* function
; to the end of the module's function list (its signature grows, so it is
; a new `Function`), so the one function below that is left untouched
; keeps its original relative position -- first -- while sample_2d/
; samplelevel_2d/load_2d end up reordered after it, in that order, once
; rewritten.
; CHECK-LABEL: define <4 x float> @sample_with_offset_unsupported(i32 %idx, i32 %sampidx, float %u, float %v) {
define <4 x float> @sample_with_offset_unsupported(i32 %idx, i32 %sampidx, float %u, float %v) {
  ; CHECK: call <4 x float> @llvm.dx.resource.sample
  ; CHECK-NOT: feme.cpu.image
  %h = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 %idx, i1 false)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampidx, i1 false)
  %coord0 = insertelement <2 x float> poison, float %u, i32 0
  %coord = insertelement <2 x float> %coord0, float %v, i32 1
  %off0 = insertelement <2 x i32> poison, i32 1, i32 0
  %off = insertelement <2 x i32> %off0, i32 0, i32 1
  %r = call <4 x float> @llvm.dx.resource.sample.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %h, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x i32> %off)
  ret <4 x float> %r
}

; CHECK-LABEL: define <4 x float> @sample_2d(
; CHECK-SAME: i32 %idx, i32 %sampidx, float %u, float %v,
; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count,
; CHECK-SAME: ptr %sampler_heap, i32 %sampler_heap_count,
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size,
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
define <4 x float> @sample_2d(i32 %idx, i32 %sampidx, float %u, float %v) {
  ; CHECK: [[U:%.*]] = extractelement <2 x float> [[COORD:%.*]], i64 0
  ; CHECK: [[V:%.*]] = extractelement <2 x float> [[COORD]], i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
  ; CHECK-SAME: ptr %image_heap, i32 %image_heap_count,
  ; CHECK-SAME: ptr %sampler_heap, i32 %sampler_heap_count,
  ; CHECK-SAME: i32 %idx, i32 %sampidx, float [[U]], float [[V]],
  ; CHECK-SAME: float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00,
  ; CHECK-SAME: float 0.000000e+00, i1 false, i1 true)
  %h = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 %idx, i1 false)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampidx, i1 false)
  %coord0 = insertelement <2 x float> poison, float %u, i32 0
  %coord = insertelement <2 x float> %coord0, float %v, i32 1
  %r = call <4 x float> @llvm.dx.resource.sample.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %h, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; An explicit-LOD sample: `use_explicit_lod` must be `true`, and the LOD
; operand must be threaded through rather than defaulted to 0.
; CHECK-LABEL: define <4 x float> @samplelevel_2d(
define <4 x float> @samplelevel_2d(i32 %idx, i32 %sampidx, float %u, float %v, float %lod) {
  ; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
  ; CHECK-SAME: {{.*}}, float %lod, i1 true, i1 true)
  %h = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 %idx, i1 false)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampidx, i1 false)
  %coord0 = insertelement <2 x float> poison, float %u, i32 0
  %coord = insertelement <2 x float> %coord0, float %v, i32 1
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %h, target("dx.Sampler", 0) %s, <2 x float> %coord, float %lod, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; An explicit-mip texel fetch (no sampler): `llvm.dx.resource.load.level`
; becomes `feme.cpu.image.load.2d.v4f32`.
; CHECK-LABEL: define <4 x float> @load_2d(
define <4 x float> @load_2d(i32 %idx, i32 %x, i32 %y, i32 %mip) {
  ; CHECK: [[X:%.*]] = extractelement <2 x i32> [[COORD2:%.*]], i64 0
  ; CHECK: [[Y:%.*]] = extractelement <2 x i32> [[COORD2]], i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.load.2d.v4f32(
  ; CHECK-SAME: ptr %image_heap, i32 %image_heap_count, i32 %idx, i32 [[X]], i32 [[Y]], i32 %mip, i32 0, i1 true)
  %h = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 %idx, i1 false)
  %coord0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 %y, i32 1
  %r = call <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_v4f32_0_0_1_2t.v2i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %h, <2 x i32> %coord, i32 %mip, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32, i1)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32, i1)
declare <4 x float> @llvm.dx.resource.sample.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 0), <2 x float>, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_v4f32_0_0_1_2t.v2i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), <2 x i32>, i32, <2 x i32>)
