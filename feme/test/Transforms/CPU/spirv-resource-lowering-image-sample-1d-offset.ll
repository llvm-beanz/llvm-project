; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(d): a real, nonzero `ConstOffset` against `Plain1D`/
; `Array1D`. `isSupportedOffset` used to reject both shapes outright
; (only `Plain2D`/`Array2D`/`Plain3D` were ever accepted); it now accepts
; them too, but as a bare scalar `i32` rather than a vector -- confirmed
; via a real `deqp-vk` SPIR-V capture that `Plain1D`'s/`Array1D`'s own
; `ConstOffset` is always scalar (SPIR-V's own `ConstOffset`
; dimensionality tracks only the image's real spatial dimension count,
; 1 for a 1D image, excluding any array layer). `lowerImageAccesses`'s
; `Plain1D`/`Array1D` branch now extracts this real scalar `Offset`
; operand instead of always assuming zero.

target triple = "spirv-unknown-vulkan-compute"

; Texture1D: [Dim=1D(0), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define <4 x float> @sample_1d_offset(
; CHECK: call <4 x float> @feme.cpu.image.sample.1d.v4f32(
define <4 x float> @sample_1d_offset(float %u) {
  %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg1d(
      target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, float %u, i32 7)
  ret <4 x float> %r
}

; Texture1DArray: [Dim=1D(0), Depth=0, Arrayed=1, MS=0, Sampled=1].
; CHECK-LABEL: define <4 x float> @sample_1darray_offset(
; CHECK: call <4 x float> @feme.cpu.image.sample.1darray.v4f32(
define <4 x float> @sample_1darray_offset(<2 x float> %uandlayer) {
  %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1darray(i32 1, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 1, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg1darray(
      target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %uandlayer, i32 7)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1darray(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.sample.v4f32.timg1d(
    target("spirv.Image", float, 0, 0, 0, 0, 1, 0), target("spirv.Sampler"),
    float, i32)
declare <4 x float> @llvm.spv.resource.sample.v4f32.timg1darray(
    target("spirv.Image", float, 0, 0, 1, 0, 1, 0), target("spirv.Sampler"),
    <2 x float>, i32)
