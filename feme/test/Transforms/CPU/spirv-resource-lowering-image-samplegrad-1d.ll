; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L65: explicit-`Grad` sampling against `Plain1D`/`Array1D`.
; Both shapes' single addressed coordinate component is a bare scalar
; float, not a vector (SPIR-V never vector-wraps a 1-component
; coordinate) -- so unlike `Array2D`/`CubeArray` (roadmap L64), no
; `ExtractElementInst` unpacking is needed: the caller's own `dPdx`/`dPdy`
; scalar operands are threaded straight through to `createSample1D`'s/
; `createSample1DArray`'s existing `DUdX`/`DUdY` operands (added by
; roadmap L63 for synthesized implicit-LOD derivatives).
;
; `hasOnlySupportedImageUses` used to reject `Grad` against these two
; shapes outright, even though the derivative-operand infrastructure
; `createSample1D`/`createSample1DArray` needed already existed --
; unlike `Plain3D`, which has no ordinary sampled-image infrastructure of
; its own at all yet (a materially bigger prerequisite, roadmap L65(a)).
;
; Roadmap L66(d) corrects this file's own trailing `ConstOffset` operand
; from a `<1 x i32>` vector to a bare scalar `i32`, matching the real,
; confirmed-via-`deqp-vk`-capture ABI `isSupportedOffset`'s own
; `AllowPlain1DArray1D` case now requires for these two shapes (this
; intrinsic's ordinary-sample `isSupportedOffset` check applies to a
; `Grad` sample too, not just a plain one) -- this file predates that
; discovery and had used the same vector shape every other (2D-and-wider)
; shape's own always-zero `Grad` offset uses.

target triple = "spirv-unknown-vulkan-compute"

; Texture1D: [Dim=1D(0), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define <4 x float> @samplegrad_1d(
; CHECK: call <4 x float> @feme.cpu.image.sample.1d.v4f32(
; CHECK-SAME: float %u, float %dpdx, float %dpdy
define <4 x float> @samplegrad_1d(float %u, float %dpdx, float %dpdy) {
  %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg1d(
      target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, float %u, float %dpdx, float %dpdy,
      i32 0)
  ret <4 x float> %r
}

; Texture1DArray: [Dim=1D(0), Depth=0, Arrayed=1, MS=0, Sampled=1].
; The array layer coordinate component has no derivative of its own,
; matching `Array2D`/`CubeArray`'s own narrowing -- but since `Array1D`'s
; own ordinary width is only 2 (`U`, `ArrayLayer`), the narrowed
; derivative is 1-wide, i.e. the same bare scalar shape `Plain1D` uses
; above, not a vector.
; CHECK-LABEL: define <4 x float> @samplegrad_1darray(
; CHECK: call <4 x float> @feme.cpu.image.sample.1darray.v4f32(
; CHECK-SAME: float {{.*}}, float {{.*}}, float %dpdx, float %dpdy
define <4 x float> @samplegrad_1darray(<2 x float> %uandlayer, float %dpdx,
                                       float %dpdy) {
  %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1darray(i32 0, i32 2, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 3, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg1darray(
      target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %uandlayer, float %dpdx,
      float %dpdy, i32 0)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1darray(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg1d(
    target("spirv.Image", float, 0, 0, 0, 0, 1, 0), target("spirv.Sampler"),
    float, float, float, i32)
declare <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg1darray(
    target("spirv.Image", float, 0, 0, 1, 0, 1, 0), target("spirv.Sampler"),
    <2 x float>, float, float, i32)
