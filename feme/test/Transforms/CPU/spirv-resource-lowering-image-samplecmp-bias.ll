; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L52(b): `llvm.spv.resource.samplecmpbias{,.clamp}` -- the
; depth-comparison counterparts of `llvm.spv.resource.samplebias{,.clamp}`
; (roadmap L58) -- are now recognized by `isDrefSampleIntrinsic`
; (SPIRVResourceLowering.cpp) through its new `HasBias` out-parameter, and
; the real `Bias` operand is threaded through as
; `createSampleCmp2D`/`createSampleCmpArray2D`/`createSampleCmpCube`/
; `createSampleCmpCubeArray`'s new `Bias` parameter, which sits directly
; after `Dref` and ahead of `ConstOffset`/`MinLodClamp` to match SPIR-V's
; own fixed Image Operands bit order.
;
; Roadmap L62: `Plain1D`/`Array1D` now support a biased `Dref` sample too,
; closing the deferred half of L52(b) -- `createSampleCmp1D`/
; `createSampleCmpArray1D` each thread a real `Bias`/`MinLodClamp` pair.
; `samplecmp_bias_1d` below pins that; it previously pinned the opposite
; (that the same call was left entirely unlowered) and is inverted here
; rather than deleted. Both shapes still carry no `ConstOffset` of their
; own, per `ImageCallKind::SampleCmp1D`'s own doc.

target triple = "spirv-unknown-vulkan-compute"

; The trailing `min_lod_clamp` reads as negative infinity (a no-op floor):
; `samplecmpbias`, unlike `samplecmpbias_clamp`, has no clamp of its own.
; CHECK-LABEL: define float @samplecmp_bias_1d(
; CHECK: call float @feme.cpu.image.samplecmp.1d.f32(
; CHECK-SAME: float %dref, float %bias, float -inf, i1 true)
define float @samplecmp_bias_1d(<3 x float> %coord, float %dref, float %bias) {
  %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 8, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 9, i32 1, i32 0, ptr null)
  %r = call float @llvm.spv.resource.samplecmpbias(
      target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      float %bias, <3 x i32> zeroinitializer)
  ret float %r
}

; Texture2D: [Dim=2D(1), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_bias(
define float @samplecmp_bias(<3 x float> %coord, float %dref, float %bias) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float %bias, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpbias(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      float %bias, <3 x i32> zeroinitializer)
  ret float %r
}

; A nonzero `ConstOffset` alongside the bias confirms the trailing
; operands really do shift by one when a bias is present -- the exact
; case `getDrefSampleOffsetIdx()` exists to get right.
; CHECK-LABEL: define float @samplecmp_bias_offset(
define float @samplecmp_bias_offset(<3 x float> %coord, float %dref, float %bias) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float %bias, i32 1, i32 -1, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpbias(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      float %bias, <3 x i32> <i32 1, i32 -1, i32 0>)
  ret float %r
}

; Texture2DArray: [Dim=2D(1), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_bias_array2d(
define float @samplecmp_bias_array2d(<4 x float> %coord, float %dref, float %bias) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 2, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float %bias, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpbias(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      float %bias, <4 x i32> zeroinitializer)
  ret float %r
}

; TextureCube: [Dim=Cube(3), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; Also carries a `MinLod` clamp, so this one exercises
; `llvm.spv.resource.samplecmpbias.clamp` and `getDrefSampleClampIdx()`.
; CHECK-LABEL: define float @samplecmp_bias_clamp_cube(
define float @samplecmp_bias_clamp_cube(<4 x float> %coord, float %dref, float %bias, float %clamp) {
  %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 4, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.cube.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float %bias, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmpbias.clamp(
      target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      float %bias, <4 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; TextureCubeArray: [Dim=Cube(3), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_bias_cubearray(
define float @samplecmp_bias_cubearray(<4 x float> %coord, float %dref, float %bias) {
  %img = call target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cubearray(i32 0, i32 6, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32 0, i32 7, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float %bias, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpbias(
      target("spirv.Image", float, 3, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      float %bias, <4 x i32> zeroinitializer)
  ret float %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.array2d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cubearray(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
