; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers feme::cpu::SPIRVResourceLoweringPass's handling of
; `llvm.spv.resource.getarraylength` (roadmap H160): a bound
; `RWStructuredBuffer<float>` handle's own bare "array length" call --
; `spirv.ArrayLength`'s conversion result, see
; feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-array-length.mlir -- reads
; no element, so (mirroring the analogous texel-buffer bare
; `getdimensions.x` special case, `isGetDimensions1Intrinsic`) it is lowered
; directly to `feme.cpu.resource.getdimensions.raw.i32` against the
; handle's own descriptor, with this handle's known per-element `Stride`
; (4, from `float`'s store size) passed through as that call's stride
; operand -- no `getpointer` indirection, and no `ElementIndex`/`Offset`
; computation, either.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %idx, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define void @main(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %len = call i32 @llvm.spv.resource.getarraylength.tspirv.VulkanBuffer_a0f32_12_1t(
      target("spirv.VulkanBuffer", [0 x float], 12, 1) %h)
  ; CHECK: call i32 @feme.cpu.resource.getdimensions.raw.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i1 true)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
  %v = sitofp i32 %len to float
  store float %v, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.raw.f32(
  ret void
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK-NOT: @llvm.spv.resource.getarraylength

declare i32 @llvm.spv.resource.getarraylength.tspirv.VulkanBuffer_a0f32_12_1t(
    target("spirv.VulkanBuffer", [0 x float], 12, 1))
