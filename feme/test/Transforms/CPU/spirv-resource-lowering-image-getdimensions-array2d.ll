; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap H124s: `OpImageQuerySize` (`llvm.spv.resource.getdimensions.xyz`,
; a `v3uint` result) against an `Array2D` *storage* image -- the exact real
; shape an `RWTexture2DArray::GetDimensions(Width, Height, Elements)` call
; produces. Unlike a *sampled* `Texture2DArray`'s identical-looking
; no-mip-argument `GetDimensions` overload (which Clang's own HLSL codegen
; always lowers to `OpImageQuerySizeLod` with an explicit `Lod = 0`, see
; `spirv-resource-lowering-image-query.ll`), a storage image has no
; mip-chain concept to select a level from, so its own `GetDimensions`
; lowers to this bare, Lod-less opcode instead -- which
; `SPIRVResourceLoweringPass` did not recognize at all before this fix,
; rejecting the whole handle (`isGetDimensions3Intrinsic` didn't exist).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <3 x i32> @storage_image_array2d_size(
define <3 x i32> @storage_image_array2d_size() {
  %img = call target("spirv.Image", float, 1, 2, 1, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i32 0, i1 true)
  %dims = call <3 x i32> @llvm.spv.resource.getdimensions.xyz.timg(
      target("spirv.Image", float, 1, 2, 1, 0, 2, 1) %img)
  ret <3 x i32> %dims
}

declare target("spirv.Image", float, 1, 2, 1, 0, 2, 1)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare <3 x i32> @llvm.spv.resource.getdimensions.xyz.timg(
    target("spirv.Image", float, 1, 2, 1, 0, 2, 1))
