; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(c): `llvm.spv.resource.samplecmpgrad{,.clamp}` -- the
; depth-comparison counterparts of `llvm.spv.resource.samplegrad{,.clamp}`
; (roadmap L59) -- are now recognized by `isDrefSampleIntrinsic`
; (SPIRVResourceLowering.cpp) through its new `HasGrad` out-parameter, and
; the real `dPdx`/`dPdy` derivative pair is threaded through as
; `femeCpuImageSampleCmp2DF32`'s new `DUdX`/`DUdY`/`DVdX`/`DVdY`
; parameters -- unlocking a real, derivative-driven implicit LOD for
; depth-comparison sampling, in place of the always-level-0 result every
; other intrinsic form still gets. `dPdx`/`dPdy` sit directly after `Dref`
; and ahead of `ConstOffset`/`MinLodClamp`, matching SPIR-V's own fixed
; Image Operands bit order (mirroring `samplecmpbias`'s own single `Bias`
; scalar in that same position).
;
; Roadmap L66(f) widens this support to `Plain1D`/`Array1D` too, whose own
; `dPdx`/`dPdy` are bare scalar floats rather than `Plain2D`'s 2-wide
; vectors (`GradDerivativeWidth`'s own generalized "arrayed shapes drop one
; component" formula, mirroring the equivalent non-`Dref` `Grad` precedent
; roadmap L64 already established) -- see `samplecmp_grad_1d`/
; `samplecmp_grad_array1d` below. Roadmap L66(g) further widens it to
; `Array2D`, whose own `dPdx`/`dPdy` are a 2-wide vector again (the same
; width as `Plain2D`, per that same generalized formula) -- see
; `samplecmp_grad_array2d` below. Roadmap L66(h) further widens it to
; `Cube`, whose own `dPdx`/`dPdy` are a genuine 3-wide direction-vector
; derivative pair (`GradDerivativeWidth`'s own unarrayed-shape case,
; `SampleCoordWidth` itself already being 3 for `Cube`) -- see
; `samplecmp_grad_cube` below. Roadmap L66(i) further widens it to
; `CubeArray`, whose own `dPdx`/`dPdy` needed no further width change at
; all (the same generalized "arrayed shapes drop one component" formula
; already resolves `CubeArray`'s 4-wide `SampleCoordWidth` down to the
; same 3-wide derivative `Cube` itself uses) -- see
; `samplecmp_grad_cubearray` below.

target triple = "spirv-unknown-vulkan-compute"

; TextureCubeArray: [Dim=Cube(3), Depth=2, Arrayed=1, MS=0, Sampled=1,
; Format=0]. Roadmap L66(i): the `CubeArray` counterpart of
; `samplecmp_grad_cube` below -- only `DirX`/`DirY`/`DirZ`, never
; `ArrayLayer` (the trailing lane of the 4-wide `Coordinate`), is ever
; differentiated, mirroring `samplecmp_grad_array2d`'s own identical
; `Array2D`-versus-`Plain2D` precedent. Its own `dPdx`/`dPdy` are a
; 3-wide direction-vector, unpacked with `CreateExtractElement` the same
; way `Cube`'s own `samplecmp_grad_cube` below is.
; CHECK-LABEL: define float @samplecmp_grad_cubearray(
define float @samplecmp_grad_cubearray(<4 x float> %coord,
                                       float %dref,
                                       <3 x float> %dpdx,
                                       <3 x float> %dpdy) {
  %img = call target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cubearray(i32 0, i32 12, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32 0, i32 13, i32 1, i32 0, ptr null)
  ; CHECK: %[[DX:.*]] = extractelement <4 x float> %coord, i64 0
  ; CHECK: %[[DY:.*]] = extractelement <4 x float> %coord, i64 1
  ; CHECK: %[[DZ:.*]] = extractelement <4 x float> %coord, i64 2
  ; CHECK: %[[LAYER:.*]] = extractelement <4 x float> %coord, i64 3
  ; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %[[DX]], float %[[DY]], float %[[DZ]], float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %[[LAYER]], float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 3, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <3 x float> %dpdx, <3 x float> %dpdy, <4 x i32> zeroinitializer)
  ret float %r
}

; Texture2D: [Dim=2D(1), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; The trailing `min_lod_clamp` reads as negative infinity (a no-op floor):
; `samplecmpgrad`, unlike `samplecmpgrad.clamp`, has no clamp of its own.
; CHECK-LABEL: define float @samplecmp_grad(
define float @samplecmp_grad(<3 x float> %coord, float %dref,
                             <2 x float> %dpdx, <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <2 x float> %dpdx, <2 x float> %dpdy, <2 x i32> zeroinitializer)
  ret float %r
}

; A nonzero `ConstOffset` alongside the gradient confirms the trailing
; operands really do shift by two (past both `dPdx` and `dPdy`) when a
; `Grad` is present -- the exact case `getDrefSampleOffsetIdx()` exists to
; get right.
; CHECK-LABEL: define float @samplecmp_grad_offset(
define float @samplecmp_grad_offset(<3 x float> %coord, float %dref,
                                    <2 x float> %dpdx, <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 1, i32 -1, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <2 x float> %dpdx, <2 x float> %dpdy, <2 x i32> <i32 1, i32 -1>)
  ret float %r
}

; Also carries a `MinLod` clamp, so this one exercises
; `llvm.spv.resource.samplecmpgrad.clamp` and `getDrefSampleClampIdx()`.
; CHECK-LABEL: define float @samplecmp_grad_clamp(
define float @samplecmp_grad_clamp(<3 x float> %coord, float %dref,
                                   <2 x float> %dpdx, <2 x float> %dpdy,
                                   float %clamp) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float %clamp, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad.clamp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <2 x float> %dpdx, <2 x float> %dpdy, <2 x i32> zeroinitializer,
      float %clamp)
  ret float %r
}

; Texture1D: [Dim=1D(0), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; Roadmap L66(f): `Plain1D`'s own `dPdx`/`dPdy` are bare scalar floats, not
; a 2-wide vector -- read directly as `DUdX`/`DUdY` with no
; `CreateExtractElement` needed. Its own `Coordinate` stays the
; pre-existing fixed 3-wide `Dref` shape (`vec3(u, <unused>, compare)`,
; the same quirk every other `Plain1D` `Dref` intrinsic already has),
; unaffected by this row.
; CHECK-LABEL: define float @samplecmp_grad_1d(
define float @samplecmp_grad_1d(<3 x float> %coord, float %dref, float %dpdx,
                                float %dpdy) {
  %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.1d(i32 0, i32 6, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.1d(i32 0, i32 7, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.1d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 2, i32 2, float %{{.*}}, float %dpdx, float %dpdy, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      float %dpdx, float %dpdy, <3 x i32> zeroinitializer)
  ret float %r
}

; Texture1DArray: [Dim=1D(0), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; Roadmap L66(f): the `Array1D` counterpart of the test just above -- only
; `U`, never `ArrayLayer`, is ever differentiated.
; CHECK-LABEL: define float @samplecmp_grad_array1d(
define float @samplecmp_grad_array1d(<3 x float> %coord, float %dref,
                                     float %dpdx, float %dpdy) {
  %img = call target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array1d(i32 0, i32 8, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array1d(i32 0, i32 9, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.1darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 3, i32 3, float %{{.*}}, float %{{.*}}, float %dpdx, float %dpdy, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 0, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      float %dpdx, float %dpdy, <3 x i32> zeroinitializer)
  ret float %r
}

; Texture2DArray: [Dim=2D(1), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; Roadmap L66(g): the `Array2D` counterpart of `samplecmp_grad` above --
; only `U`/`V`, never `ArrayLayer` (the trailing lane of the 4-wide
; `Coordinate`), is ever differentiated, mirroring
; `samplecmp_grad_array1d`'s own identical `Array1D` precedent. Its own
; `dPdx`/`dPdy` are a 2-wide vector, extracted with `CreateExtractElement`
; the same way `Plain2D`'s own `samplecmp_grad` above is.
; CHECK-LABEL: define float @samplecmp_grad_array2d(
define float @samplecmp_grad_array2d(<4 x float> %coord, float %dref,
                                     <2 x float> %dpdx, <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 10, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 11, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 4, i32 4, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <2 x float> %dpdx, <2 x float> %dpdy, <3 x i32> zeroinitializer)
  ret float %r
}

; TextureCube: [Dim=Cube(3), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; Roadmap L66(h): the `Cube` counterpart of `samplecmp_grad` above --
; `dPdx`/`dPdy` are a genuine 3-wide direction-vector derivative pair
; (`GradDerivativeWidth`'s own unarrayed-shape case, `SampleCoordWidth`
; itself already being 3 for `Cube`), unpacked into six scalars the same
; way `createSampleCube`'s own `Grad` handling unpacks an ordinary
; sample's derivative pair (roadmap L59). No `ConstOffset` (SPIR-V
; forbids `ConstOffset` against `Dim::Cube`), but `min_lod_clamp` reads as
; negative infinity (a no-op floor) the same way `samplecmp_grad`'s own
; trailing operand does above.
; CHECK-LABEL: define float @samplecmp_grad_cube(
define float @samplecmp_grad_cube(<4 x float> %coord, float %dref,
                                  <3 x float> %dpdx, <3 x float> %dpdy) {
  %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 4, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.cube.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 1, i32 1, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <3 x float> %dpdx, <3 x float> %dpdy, <4 x i32> zeroinitializer)
  ret float %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cubearray(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cubearray(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.1d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.1d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.array1d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.array1d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.array2d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32, i32, i32, i32, ptr)
