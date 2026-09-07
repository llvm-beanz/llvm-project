; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(k): `Plain1D` depth-comparison (shadow) sampling now accepts a
; real, nonzero `ConstOffset`, mirroring `Plain2D`'s own `samplecmp_offset`
; case in spirv-resource-lowering-image-samplecmp.ll and `Array2D`'s own
; `samplecmp_array2d_offset` case in
; spirv-resource-lowering-image-samplecmp-shapes.ll. Unlike those two
; shapes' own 2-component `ConstOffset` operand, `Plain1D`'s own
; depth-comparison `ConstOffset` is a bare scalar `i32` at the
; `llvm.spv.resource.samplecmp` intrinsic call itself (confirmed via a real
; IR reduction, mirroring `spirv-resource-lowering-image-sample-1d-offset.ll`'s
; own identical, already-landed precedent for the *ordinary* (non-`Dref`)
; sample path, roadmap L66(d)) -- distinct from this same intrinsic's own
; `Coordinate` operand, which (per `ImageSampleDrefImplicitLodPattern`'s own
; comment, SPIRVToLLVMPatterns.cpp) always stays a genuine, `Dref`-padded
; vector even for this shape.
;
; `Array1D`'s own identical fix is pinned separately, in
; spirv-resource-lowering-image-samplecmp-array1d-offset.ll -- combining
; both shapes' own `llvm.spv.resource.samplecmp` calls (as opposed to
; `.samplecmpbias`/`.samplecmpgrad`, each already combined successfully in
; their own sibling files) in a single module trips an unrelated,
; pre-existing LLVM intrinsic-overload-mangling quirk for this specific
; intrinsic name once two different `Dim1D`-family image handle types
; coexist, unrelated to this row's own fix (confirmed via manual
; `feme-opt` runs isolating each shape alone, and even a same-shape,
; already-supported zero-vector-offset pair, as still tripping it) --
; kept as two single-shape files instead.

target triple = "spirv-unknown-vulkan-compute"

; Texture1D: [Dim=1D(0), Depth=2, Arrayed=0, MS=0, Sampled=1, Format=0].
; CHECK-LABEL: define float @samplecmp_1d_offset(
define float @samplecmp_1d_offset(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp1d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.1d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 {{[0-9]+}}, i32 {{[0-9]+}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 1, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 0, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      i32 1)
  ret float %r
}

declare target("spirv.Image", float, 0, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp1d(i32, i32, i32, i32, ptr)
