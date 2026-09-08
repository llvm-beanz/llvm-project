; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L70: `OpImageQuerySize` (GLSL's own `textureSize()`/`imageSize()`
; against a `sampler2D`/`image2D` with no explicit LOD argument,
; `llvm.spv.resource.getdimensions.xy`) against a plain 2D sampled image and
; a plain 2D storage image -- the real shape a
; `dEQP-VK.glsl.texture_functions.texturelod.sampler2d_float_compute`-style
; compute shader's own `imageSize(destImage)` bounds check produces. Neither
; handle's `getdimensions` call used to be a use `SPIRVResourceLoweringPass`
; recognized at all -- `collectHandles` bailed to `std::nullopt` for the
; *whole function* the moment it saw one, leaving every handle in that same
; function (including a perfectly ordinary sample/fetch/store on some other
; binding) unnormalized, which is what `checkSupportedRaisedOps` then
; rejected wholesale.

target triple = "spirv-unknown-vulkan-compute"

; A plain 2D sampled image's own `textureSize()` query.

; CHECK-LABEL: define <2 x i32> @sampled_image_size(
define <2 x i32> @sampled_image_size() {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i1 true)
  %dims = call <2 x i32> @llvm.spv.resource.getdimensions.xy.timg(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img)
  ret <2 x i32> %dims
}

; A plain 2D storage image's own `imageSize()` query, alongside an ordinary
; `OpImageWrite` on the same handle -- the exact real shape (roadmap L70's
; own reduction of `dEQP-VK.glsl.texture_functions.texturelod.sampler2d_
; float_compute`'s compute shader) that used to fail: the `getdimensions`
; call alone (not the store) was the unrecognized use that made
; `collectHandles` reject this whole handle -- and therefore this whole
; function, store included -- before this row's own fix.

; CHECK-LABEL: define void @storage_image_size_and_store(
define void @storage_image_size_and_store(<2 x i32> %coord, <4 x float> %texel) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding.timg2(i32 0, i32 4, i32 1, i32 0, ptr null)
  ; CHECK-DAG: call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i1 true)
  %dims = call <2 x i32> @llvm.spv.resource.getdimensions.xy.timg2(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %img)
  %ptr = call ptr @llvm.spv.resource.getpointer.timg2(
      target("spirv.Image", float, 1, 0, 0, 0, 2, 4) %img, <2 x i32> %coord)
  ; CHECK-DAG: call void @feme.cpu.image.store.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i32 %{{.*}}, i32 %{{.*}}, <4 x float> %texel, i1 true)
  store <4 x float> %texel, ptr %ptr
  ret void
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare <2 x i32> @llvm.spv.resource.getdimensions.xy.timg(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0))

declare target("spirv.Image", float, 1, 0, 0, 0, 2, 4)
    @llvm.spv.resource.handlefrombinding.timg2(i32, i32, i32, i32, ptr)
declare <2 x i32> @llvm.spv.resource.getdimensions.xy.timg2(
    target("spirv.Image", float, 1, 0, 0, 0, 2, 4))
declare ptr @llvm.spv.resource.getpointer.timg2(
    target("spirv.Image", float, 1, 0, 0, 0, 2, 4), <2 x i32>)
