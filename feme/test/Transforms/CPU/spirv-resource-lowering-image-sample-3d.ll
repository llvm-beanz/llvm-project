; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(a): ordinary (non-`Bias`/`MinLodClamp`/`Grad`) sampling
; against `Plain3D` (`Texture3D`). `classifySampledImage2DHandle` used to
; reject `SPIRVDim3D` outright (only `1D`/`2D`/`Cube` were ever
; recognized); it now maps a non-arrayed `Dim3D` handle to
; `ImageShape::Plain3D`, and `lowerImageAccesses`'s new early-continue
; branch for that shape extracts a real 3-component `(U, V, W)`
; coordinate (mirroring `Plain1D`/`Array1D`'s own early-continue
; precedent) and calls the new `createSample3D`, synthesizing each axis's
; own implicit-LOD screen-space derivative independently via the existing
; `getOrSynthesizeSample1DDerivatives` helper (called once per axis).

target triple = "spirv-unknown-vulkan-compute"

; Texture3D: [Dim=3D(2), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define <4 x float> @sample_3d(
; CHECK: call <4 x float> @feme.cpu.image.sample.3d.v4f32(
define <4 x float> @sample_3d(<3 x float> %coord) {
  %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
      target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> zeroinitializer)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
    target("spirv.Image", float, 2, 0, 0, 0, 1, 0), target("spirv.Sampler"),
    <3 x float>, <3 x i32>)
