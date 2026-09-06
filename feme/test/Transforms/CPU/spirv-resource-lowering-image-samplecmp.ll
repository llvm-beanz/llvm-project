; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap L46's first slice: `llvm.spv.resource.samplecmp`/
; `.samplecmplevelzero` (`spirv.ImageSampleDrefImplicitLod`/
; `ImageSampleDrefExplicitLod`'s own LLVM-SPIRV-backend intrinsics, roadmap
; L25) against a `Plain2D` sampled image, lowered into the pre-existing
; (previously unused) `feme.cpu.image.samplecmp.2d.f32` call
; (`createSampleCmp2D`, ImageCalls.h) -- unlike an ordinary filtered
; sample, both share a fixed `(image, sampler, coord, dref, offset)`
; operand shape with no `ExplicitLod`-dependent index shift of their own
; (see `isDrefSampleIntrinsic`'s comment in SPIRVResourceLowering.cpp).
; Per SPIR-V's own validation rules, a depth-comparison sample's own
; `Coordinate` operand is one component wider than its shape's ordinary
; addressing width (`<3 x float>`, not `<2 x float>`, for `Plain2D`) --
; glslang always emits this for e.g. `texture(sampler2DShadow, vec3(u, v,
; compare))`, redundantly packing the depth-reference value alongside
; `Dref` itself (a separate operand); only the first two components are
; ever read as the real U/V address. `samplecmp_clamp`'s own trailing
; `MinLod` clamp operand is now covered by roadmap L52(c) -- see
; spirv-resource-lowering-image-samplecmp-clamp.ll. A nonzero `ConstOffset` is exercised by
; spirv-resource-lowering-image-samplecmp-shapes.ll's own
; `samplecmp_offset` case instead (roadmap L50d); this file's own two
; cases below both pass an all-zero `<3 x i32>` offset, lowered to a
; constant `i32 0, i32 0` pair.

target triple = "spirv-unknown-vulkan-compute"

; An implicit-LOD depth-comparison sample: `use_explicit_lod` is `false`,
; and the constant `Lod` operand passed to `createSampleCmp2D` is always
; zero (`femeCpuImageSampleCmp2DF32` has no derivative inputs of its own
; to compute a real implicit LOD from, see `isDrefSampleIntrinsic`'s
; comment -- a pre-existing narrowing, not something this row changes).

; CHECK-LABEL: define float @samplecmp(
; CHECK-SAME: <3 x float> %coord, float %dref, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define float @samplecmp(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <3 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <3 x float> %coord, i64 1
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float 0.000000e+00, i1 false, float %dref, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> zeroinitializer)
  ret float %r
}

; `SampleCmpLevelZero`'s own explicit-LOD-zero shape: `use_explicit_lod` is
; `true`, still with the same constant-zero `Lod` operand -- this
; intrinsic has no LOD operand of its own to thread through at all (it
; always forces mip level 0).

; CHECK-LABEL: define float @samplecmplevelzero(
define float @samplecmplevelzero(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 true, float %dref, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmplevelzero(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> zeroinitializer)
  ret float %r
}

; Roadmap L50d: a real, nonzero, compile-time-constant `ConstOffset` --
; `createSampleCmp2D` now has an `OffsetX`/`OffsetY` pair mirroring
; `createSample2D`'s own roadmap L26 support, split from the offset
; operand's own two components the same way `Coord`'s `U`/`V` are.
; `isSupportedOffset` (SPIRVResourceLowering.cpp) only accepts an offset
; that is already a real `Constant` -- matching SPIR-V's own
; `ConstOffset` image operand, which is inherently a compile-time
; constant per the spec, so the offset below is a literal `<3 x i32>`
; rather than a runtime `insertelement` chain (which InstCombine would
; ordinarily fold away before this pass ever sees it in a real compiled
; module, but this pass runs alone here with no such folding).
; CHECK-LABEL: define float @samplecmp_offset(
define float @samplecmp_offset(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, i1 false, float %dref, i32 1, i32 -1, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> <i32 1, i32 -1, i32 0>)
  ret float %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
