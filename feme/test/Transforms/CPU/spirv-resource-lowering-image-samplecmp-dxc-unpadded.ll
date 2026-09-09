; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap L7b: unlike glslang, `dxc`'s own real SPIR-V output for
; HLSL's `Texture2D::SampleCmp`/`SampleCmpLevelZero` (confirmed via a real
; `vk::SampledTexture2D`+`SampleCmp` repro compiled with
; `dxc -fspv-target-env=vulkan1.3` and a real `check-hlsl-feme-vk`
; offload-test-suite run of `Vk.SampledTexture2D.SampleCmp.test.yaml`)
; does *not* pad its Coordinate operand with a redundant, unused trailing
; component the way glslang always does (see
; spirv-resource-lowering-image-samplecmp.ll's own comment) -- it stays
; exactly the shape's ordinary, unpadded addressing width (`<2 x float>`
; for `Plain2D`), with `Dref` arriving purely through its own separate
; operand. `hasOnlySupportedImageUses` (SPIRVResourceLowering.cpp) now
; accepts both widths for every shape but `Plain1D` (whose own
; unconditional, not-shape-gated `C0`/`C1` extraction in
; `lowerImageAccesses` cannot consume a bare scalar coordinate at all --
; see that file's own comment); `lowerImageAccesses`'s own codegen half
; already tolerated either width transparently even before this fix, via
; its fixed-index (not fixed-total-width) component extraction.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define float @samplecmp_dxc_unpadded(
; CHECK-SAME: <2 x float> %coord, float %dref, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define float @samplecmp_dxc_unpadded(<2 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <2 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <2 x float> %coord, i64 1
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, float %dref, float 0.000000e+00, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, float %dref,
      <2 x i32> zeroinitializer)
  ret float %r
}

; The `SampleCmpLevelZero` counterpart (`use_explicit_lod` forced `true`)
; with the same unpadded coordinate width.

; CHECK-LABEL: define float @samplecmplevelzero_dxc_unpadded(
define float @samplecmplevelzero_dxc_unpadded(<2 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call float @feme.cpu.image.samplecmp.2d.f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %dref, float 0.000000e+00, i32 0, i32 0, float -inf, i1 true)
  %r = call float @llvm.spv.resource.samplecmplevelzero(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, float %dref,
      <2 x i32> zeroinitializer)
  ret float %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
