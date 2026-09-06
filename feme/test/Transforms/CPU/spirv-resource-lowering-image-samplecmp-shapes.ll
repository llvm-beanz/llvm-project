; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap L48's Array2D/Cube/CubeArray slice: `llvm.spv.resource.
; samplecmp`/`.samplecmplevelzero` against a `Texture2DArray`/`TextureCube`/
; `TextureCubeArray` sampled image, lowered into the new
; `feme.cpu.image.samplecmp.2darray.f32`/`.samplecmp.cube.f32`/
; `.samplecmp.cubearray.f32` calls (`createSampleCmpArray2D`/
; `createSampleCmpCube`/`createSampleCmpCubeArray`, ImageCalls.h),
; mirroring spirv-resource-lowering-image-samplecmp.ll's own `Plain2D`
; coverage but with each shape's own wider `Dref` coordinate operand (see
; `DrefCoordWidth` in SPIRVResourceLowering.cpp): `Array2D`'s 4-component
; `(u, v, layer, dref-pad)`, `Cube`'s 4-component `(dx, dy, dz, dref-pad)`,
; and `CubeArray`'s own 4-component `(dx, dy, dz, dref-pad)` (already at
; SPIR-V's max vector width, so no further widening beyond `Cube`'s own).
; `samplecmp_clamp`, a `Bias` operand, and `Plain1D`/`Array1D` shapes all
; remain unsupported -- see
; spirv-resource-lowering-image-samplecmp-unsupported.ll (roadmap L48
; follow-on). A nonzero `ConstOffset` (roadmap L50d) is now supported for
; `Array2D` too (see `samplecmp_array2d_offset` below), mirroring
; `Plain2D`'s own support in spirv-resource-lowering-image-samplecmp.ll's
; `samplecmp_offset`; `Cube`/`CubeArray` can never carry one (SPIR-V
; forbids `ConstOffset` against `Dim::Cube`).

target triple = "spirv-unknown-vulkan-compute"

; Texture2DArray: [Dim=2D(1), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_array2d(
; CHECK-SAME: <4 x float> %coord, float %dref
define float @samplecmp_array2d(<4 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <4 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <4 x float> %coord, i64 1
  ; CHECK: %[[LAYER:.*]] = extractelement <4 x float> %coord, i64 2
  ; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %[[U]], float %[[V]], float %[[LAYER]], float 0.000000e+00, i1 false, float %dref, i32 0, i32 0, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer)
  ret float %r
}

; Roadmap L50d: a real, nonzero `ConstOffset` against `Array2D`, mirroring
; `Plain2D`'s own `samplecmp_offset` case in
; spirv-resource-lowering-image-samplecmp.ll -- a literal `<4 x i32>`
; constant, not a runtime `insertelement` chain, since `isSupportedOffset`
; only accepts an already-constant offset (see that file's own comment).
; CHECK-LABEL: define float @samplecmp_array2d_offset(
define float @samplecmp_array2d_offset(<4 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, i32 1, i32 -1, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> <i32 1, i32 -1, i32 0, i32 0>)
  ret float %r
}

; TextureCube: [Dim=Cube(3), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_cube(
define float @samplecmp_cube(<4 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 2, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: %[[DX:.*]] = extractelement <4 x float> %coord, i64 0
  ; CHECK: %[[DY:.*]] = extractelement <4 x float> %coord, i64 1
  ; CHECK: %[[DZ:.*]] = extractelement <4 x float> %coord, i64 2
  ; CHECK: call float @feme.cpu.image.samplecmp.cube.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %[[DX]], float %[[DY]], float %[[DZ]], float 0.000000e+00, i1 false, float %dref, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer)
  ret float %r
}

; TextureCubeArray: [Dim=Cube(3), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_cubearray(
define float @samplecmp_cubearray(<4 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cubearray(i32 0, i32 4, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32 0, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: %[[DX:.*]] = extractelement <4 x float> %coord, i64 0
  ; CHECK: %[[DY:.*]] = extractelement <4 x float> %coord, i64 1
  ; CHECK: %[[DZ:.*]] = extractelement <4 x float> %coord, i64 2
  ; CHECK: %[[LAYER:.*]] = extractelement <4 x float> %coord, i64 3
  ; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %[[DX]], float %[[DY]], float %[[DZ]], float %[[LAYER]], float 0.000000e+00, i1 false, float %dref, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 3, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <4 x i32> zeroinitializer)
  ret float %r
}

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
