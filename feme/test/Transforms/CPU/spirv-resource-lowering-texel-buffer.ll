; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; V4: covers feme::cpu::SPIRVResourceLoweringPass's texel-buffer support --
; a `Dim::Buffer` `target("spirv.Image", ...)` handle, the shape LLVM's
; SPIRV backend materializes for a Vulkan uniform/storage texel buffer (see
; classifyTexelBufferHandle's comment) -- lowered directly into the
; canonical `feme.cpu.resource.load.typed.v4f32`/`store.typed.v4f32` calls,
; the same runtime helpers a DXIL typed buffer already uses. Sampled == 1
; ("used with a sampler") is a uniform texel buffer, read-only; Sampled == 2
; ("used without a sampler") is a storage texel buffer, read-write.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <4 x float> @storage_texel_buffer(
; CHECK-SAME: i32 %idx, <4 x float> %v, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define <4 x float> @storage_texel_buffer(i32 %idx, <4 x float> %v) {
  %h = call target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", <4 x float>, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  ; CHECK: [[IDX:%.*]] = zext i32 %idx to i64
  %loaded = load <4 x float>, ptr %ptr
  ; CHECK: call <4 x float> @feme.cpu.resource.load.typed.v4f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], i1 true)
  store <4 x float> %v, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.typed.v4f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[IDX]], <4 x float> %v, i1 true)
  ret <4 x float> %loaded
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK: !feme.cpu.resources = !{![[RMD:[0-9]+]]}
; CHECK: !feme.cpu.bound_resources = !{![[BMD:[0-9]+]]}
; CHECK: ![[RMD]] = !{!"storage_texel_buffer", i32 0, i1 false, i32 0, i32 0}
; CHECK: ![[BMD]] = !{!"storage_texel_buffer", i32 1, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0}
