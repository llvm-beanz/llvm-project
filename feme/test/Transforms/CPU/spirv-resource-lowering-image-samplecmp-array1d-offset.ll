; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(k): `Array1D`'s own counterpart of
; spirv-resource-lowering-image-samplecmp-1d-offset.ll's `Plain1D` fix --
; see that file's own doc for why these two shapes are pinned separately
; rather than combined in one file. The array layer (`Coordinate`'s second
; component) is never offset, mirroring `createSample1DArray`'s own
; identical precedent for an ordinary sample.

target triple = "spirv-unknown-vulkan-compute"

; Texture1DArray: [Dim=1D(0), Depth=2, Arrayed=1, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_array1d_offset(
define float @samplecmp_array1d_offset(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array1d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array1d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.1darray.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 -1, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 0, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      i32 -1)
  ret float %r
}

declare target("spirv.Image", float, 0, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.array1d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.array1d(i32, i32, i32, i32, ptr)
