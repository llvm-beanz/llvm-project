; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap H124q: `spv_resource_gather`/`spv_resource_gather_cmp`
; against an `Array2D`-shaped handle (HLSL's `Texture2DArray::
; Gather{,Red,Green,Blue,Alpha}()`/`GatherCmp()`, confirmed via a real
; `check-hlsl-feme-vk` offload-test-suite run of
; `Vk.SampledTexture2DArray.Gather.test.yaml`/`GatherCmp.test.yaml`) lower
; to `feme.cpu.image.gather.array2d.v4f32`/
; `feme.cpu.image.gathercmp.array2d.v4f32`. Mirrors
; spirv-resource-lowering-image-gathercmp-dxc-unpadded.ll's own `Plain2D`
; siblings, except the coordinate is `Array2D`'s own 3-component `(U, V,
; ArrayLayer)` shape (mirroring `Sample2DArray`'s identical convention)
; rather than `Plain2D`'s 2-component one.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <4 x float> @gathercmp_array2d(
define <4 x float> @gathercmp_array2d(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <3 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <3 x float> %coord, i64 1
  ; CHECK: %[[LAYER:.*]] = extractelement <3 x float> %coord, i64 2
  ; CHECK: call <4 x float> @feme.cpu.image.gathercmp.array2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float %[[LAYER]], float %dref, i32 0, i32 0, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather.cmp(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; CHECK-LABEL: define <4 x float> @gather_array2d(
define <4 x float> @gather_array2d(<3 x float> %coord, i32 %component) {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.array2d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[U:.*]] = extractelement <3 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <3 x float> %coord, i64 1
  ; CHECK: %[[LAYER:.*]] = extractelement <3 x float> %coord, i64 2
  ; CHECK: call <4 x float> @feme.cpu.image.gather.array2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float %[[LAYER]], i32 %component, i32 0, i32 0, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather(
      target("spirv.Image", float, 1, 2, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, i32 %component,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 1, 2, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.array2d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.array2d(i32, i32, i32, i32, ptr)
