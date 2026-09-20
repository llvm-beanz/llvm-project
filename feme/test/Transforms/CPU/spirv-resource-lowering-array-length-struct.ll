; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L124(a): a real, multi-field storage-buffer block (not a
; one-member wrapper) whose own last member is the runtime array --
; `dEQP-VK.compute.pipeline.basic.read_unbound_ssbo`'s own real shape,
; `struct SSBO_1 { vec4 data; uint not_set[]; }` -- classifies as
; `HandleKind::StorageStruct`, not `HandleKind::Storage`
; (spirv-resource-lowering-array-length.ll's own one-member-wrapper case).
; Unlike that wrapper case, `BoundHandle::Stride` is not meaningful here
; (`classifyVulkanBufferHandle` always reports it as `0` for a
; `StorageStruct` handle), so `lowerAccesses` derives both operands fresh
; from `BH.ElementStruct`'s own last member instead: the runtime array's
; element stride (4, from `i32`'s store size) and that member's own
; declared byte offset (16, from `<4 x float>`'s 16-byte size) as the
; byte prefix to subtract before dividing.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @main(
define void @main(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", { <4 x float>, [0 x i32] }, 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  %len = call i32 @llvm.spv.resource.getarraylength.tspirv.VulkanBuffer_sl_v4f32a0i32s_12_1t(
      target("spirv.VulkanBuffer", { <4 x float>, [0 x i32] }, 12, 1) %h)
  ; CHECK: call i32 @feme.cpu.resource.getdimensions.raw.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i64 16, i1 true)
  %v = bitcast i32 %len to float
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", { <4 x float>, [0 x i32] }, 12, 1) %h, i32 0)
  store float %v, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.raw.f32(
  ret void
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK-NOT: @llvm.spv.resource.getarraylength

declare i32 @llvm.spv.resource.getarraylength.tspirv.VulkanBuffer_sl_v4f32a0i32s_12_1t(
    target("spirv.VulkanBuffer", { <4 x float>, [0 x i32] }, 12, 1))
