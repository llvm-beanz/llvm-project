; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L52(c): `llvm.spv.resource.samplecmp.clamp`'s own trailing
; `MinLod` clamp operand is now recognized by `isDrefSampleIntrinsic`
; (SPIRVResourceLowering.cpp), mirroring `isSampleIntrinsic`'s own
; `HasMinLodClamp` precedent for an ordinary sample (roadmap L26) --
; `Plain2D`/`Array2D`/`Cube`/`CubeArray` (each already covered by
; spirv-resource-lowering-image-samplecmp.ll's/spirv-resource-lowering-
; image-samplecmp-shapes.ll's own plain `samplecmp`/`samplecmplevelzero`
; coverage) now also lower a `samplecmp_clamp` call, threading the real
; clamp value through as `createSampleCmp2D`/`createSampleCmpArray2D`/
; `createSampleCmpCube`/`createSampleCmpCubeArray`'s new `MinLodClamp`
; parameter, instead of leaving the call entirely unrewritten the way
; this project's earlier sessions left it (this file used to be named
; spirv-resource-lowering-image-samplecmp-unsupported.ll and asserted the
; opposite -- see git history). Roadmap L62 has since done the same for
; `Plain1D`/`Array1D`: both now thread a real `MinLod` clamp through
; `createSampleCmp1D`/`createSampleCmpArray1D`, so `samplecmp_clamp_1d`
; below now pins a real lowering where it once pinned its absence.

target triple = "spirv-unknown-vulkan-compute"

; Roadmap L62: `Plain1D` (`Dim::1D(0)`) now carries a real `MinLod`
; clamp. The `bias` slot ahead of it reads as a zero constant, a no-op
; LOD shift: `samplecmp_clamp`, unlike `samplecmpbias_clamp`, has no bias
; operand of its own. Both 1D shapes still carry no `ConstOffset`.
; CHECK-LABEL: define float @samplecmp_clamp_1d(
; CHECK: call float @feme.cpu.image.samplecmp.1d.f32(
; CHECK-SAME: float %dref, float 0.000000e+00, float %clamp, i1 true)
define float @samplecmp_clamp_1d(<3 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 8, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 9, i32 1, i32 0, ptr null)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; CHECK-LABEL: define float @samplecmp_clamp(
define float @samplecmp_clamp(<3 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; Texture2DArray: [Dim=2D(1), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_clamp_array2d(
define float @samplecmp_clamp_array2d(<4 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 2, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; TextureCube: [Dim=Cube(3), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_clamp_cube(
define float @samplecmp_clamp_cube(<4 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 4, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.cube.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; TextureCubeArray: [Dim=Cube(3), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_clamp_cubearray(
define float @samplecmp_clamp_cubearray(<4 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cubearray(i32 0, i32 6, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32 0, i32 7, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 3, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer, float %clamp)
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
