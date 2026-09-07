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
; Only `Plain2D` supports this today -- `hasOnlySupportedImageUses`'s own
; new `DrefHasGrad` restriction leaves every other depth-comparison-capable
; shape (`Array2D`/`Cube`/`CubeArray`/`Plain1D`/`Array1D`) unrewritten,
; matching this project's own "extend one shape at a time" precedent (see
; `samplecmp_grad_cube_unsupported` below).

target triple = "spirv-unknown-vulkan-compute"

; TextureCube: [Dim=Cube(3), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; Roadmap L66(c) deliberately scopes `Grad` support to `Plain2D` only --
; every other depth-comparison-capable shape's own `samplecmpgrad` call is
; left entirely unrewritten, mirroring how this project has always widened
; `Dref` support one shape at a time (see roadmap L46's own initial
; `Plain2D`-only scope for ordinary `Dref` sampling). This function stays
; unrewritten by the pass and so is emitted first, ahead of every other
; (rewritten) function below -- hence this check comes first too.
; CHECK-LABEL: define float @samplecmp_grad_cube_unsupported(
; CHECK-NOT: call float @feme.cpu.image.samplecmp.cube.f32(
; CHECK: call float @llvm.spv.resource.samplecmpgrad{{[.a-zA-Z0-9_]*}}(
define float @samplecmp_grad_cube_unsupported(<4 x float> %coord, float %dref,
                                              <2 x float> %dpdx,
                                              <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 4, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 5, i32 1, i32 0, ptr null)
  %r = call float @llvm.spv.resource.samplecmpgrad(
      target("spirv.Image", float, 3, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord, float %dref,
      <2 x float> %dpdx, <2 x float> %dpdy, <4 x i32> zeroinitializer)
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

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cube(i32, i32, i32, i32, ptr)
