; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L9: a scalar (single-channel-format) texel-buffer element -- the
; shape `RWBuffer<int>`/`RWBuffer<float>` (R32_SINT/R32_FLOAT) need, since
; SPIR-V's `OpImageWrite` Texel operand takes exactly the shader-declared
; element shape (a bare scalar here) even though `OpImageRead`/`OpImageFetch`
; always return a full <4 x T> regardless of the underlying format's real
; channel count (see the neighboring spirv-resource-lowering-texel-buffer.ll,
; and `isSupportedTexelElementType`'s comment in SPIRVResourceLowering.cpp).
; Lowers to the scalar-mangled `.i32`/`.f32` typed calls, distinct from the
; `.v4i32`/`.v4f32` calls a <4 x T>-element texel buffer lowers to.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @scalar_int_storage_texel_buffer(
; CHECK-SAME: i32 %idx, i32 %v, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define i32 @scalar_int_storage_texel_buffer(i32 %idx, i32 %v) {
  %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  ; CHECK: [[IDX:%.*]] = zext i32 %idx to i64
  %loaded = load i32, ptr %ptr
  ; CHECK: call i32 @feme.cpu.resource.load.typed.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], i1 true)
  store i32 %v, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.typed.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], i32 %v, i1 true)
  ret i32 %loaded
}

declare target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1), i32)

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
