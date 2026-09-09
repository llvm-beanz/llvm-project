; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap L7d: `spv_resource_gather_cmp` (the LLVM intrinsic
; `ImageDrefGatherPattern`, SPIRVToLLVMPatterns.cpp, emits for `dxc`'s own
; real SPIR-V output for HLSL's `Texture2D::GatherCmp`, confirmed via a
; real `check-hlsl-feme-vk` offload-test-suite run of
; `Vk.SampledTexture2D.GatherCmp.test.yaml`) lowers to
; `feme.cpu.image.gathercmp.2d.v4f32`. Like
; spirv-resource-lowering-image-samplecmp-dxc-unpadded.ll's own dref-sample
; siblings, the real dxc-emitted `Coordinate` is never dref-padded, so this
; uses a plain, unpadded `<2 x float>` coordinate. Unlike a sample, a
; gather has no `Bias`/`Lod`/`Grad`/`MinLod` image operand at all (a
; gather always operates at mip level 0 per the SPIR-V spec), so
; `feme.cpu.image.gathercmp.2d.v4f32`'s own operand list is correspondingly
; narrower than `feme.cpu.image.samplecmp.2d.f32`'s.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <4 x float> @gathercmp_dxc_unpadded(
; CHECK-SAME: <2 x float> %coord, float %dref, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define <4 x float> @gathercmp_dxc_unpadded(<2 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <2 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <2 x float> %coord, i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.gathercmp.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float %dref, i32 0, i32 0, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather.cmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, float %dref,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; The `ConstOffset` shape threads the real offset through instead of the
; defaulted zero constants above.

; CHECK-LABEL: define <4 x float> @gathercmp_dxc_unpadded_const_offset(
define <4 x float> @gathercmp_dxc_unpadded_const_offset(<2 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call <4 x float> @feme.cpu.image.gathercmp.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %{{.*}}, float %{{.*}}, float %dref, i32 1, i32 0, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather.cmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, float %dref,
      <2 x i32> <i32 1, i32 0>)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
