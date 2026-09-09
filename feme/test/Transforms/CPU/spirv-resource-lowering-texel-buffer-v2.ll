; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L7a: a genuine 2-channel texel-buffer element -- the shape
; `RWBuffer<float2>`/`RWBuffer<int2>` (R32G32_FLOAT/R32G32_SINT) need.
; Confirmed reachable via a real IR reduction of
; `Basic/Matrix/matrix_m-based_getter.test`'s own `RWBuffer<float2> OutVec2`
; write (`OutVec2[0] = m._m00_m01;` lowers `OpImageWrite`'s Texel operand to
; a bare `<2 x float>`, not the `<4 x float>` this file's own
; isSupportedTexelElementType comment previously assumed was the only
; multi-component shape reachable here) -- see that function's own updated
; comment in SPIRVResourceLowering.cpp. Lowers to the `.v2f32`/`.v2i32`
; -mangled typed calls, distinct from both the scalar `.f32`/`.i32` calls
; (spirv-resource-lowering-texel-buffer-scalar.ll) and the full `.v4f32`/
; `.v4i32` calls (spirv-resource-lowering-texel-buffer.ll) a <4 x T>-element
; texel buffer lowers to.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <2 x float> @v2f32_storage_texel_buffer(
; CHECK-SAME: i32 %idx, <2 x float> %v, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define <2 x float> @v2f32_storage_texel_buffer(i32 %idx, <2 x float> %v) {
  %h = call target("spirv.Image", float, 5, 0, 0, 0, 2, 4)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 4) %h, i32 %idx)
  ; CHECK: [[IDX:%.*]] = zext i32 %idx to i64
  %loaded = load <2 x float>, ptr %ptr
  ; CHECK: call <2 x float> @feme.cpu.resource.load.typed.v2f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], i1 true)
  store <2 x float> %v, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.typed.v2f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], <2 x float> %v, i1 true)
  ret <2 x float> %loaded
}

declare target("spirv.Image", float, 5, 0, 0, 0, 2, 4)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 0, 0, 0, 2, 4), i32)

; CHECK-LABEL: define <2 x i32> @v2i32_storage_texel_buffer(
; CHECK-SAME: i32 %idx2, <2 x i32> %v2, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define <2 x i32> @v2i32_storage_texel_buffer(i32 %idx2, <2 x i32> %v2) {
  %h2 = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 5)
      @llvm.spv.resource.handlefrombinding.i32(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr2 = call ptr
      @llvm.spv.resource.getpointer.i32(target("spirv.Image", i32, 5, 0, 0, 0, 2, 5) %h2, i32 %idx2)
  ; CHECK: [[IDX2:%.*]] = zext i32 %idx2 to i64
  %loaded2 = load <2 x i32>, ptr %ptr2
  ; CHECK: call <2 x i32> @feme.cpu.resource.load.typed.v2i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 [[IDX2]], i1 true)
  store <2 x i32> %v2, ptr %ptr2
  ; CHECK: call void @feme.cpu.resource.store.typed.v2i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 [[IDX2]], <2 x i32> %v2, i1 true)
  ret <2 x i32> %loaded2
}

declare target("spirv.Image", i32, 5, 0, 0, 0, 2, 5)
    @llvm.spv.resource.handlefrombinding.i32(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.i32(target("spirv.Image", i32, 5, 0, 0, 0, 2, 5), i32)

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
