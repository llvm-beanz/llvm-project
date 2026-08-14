; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers feme::cpu::SPIRVResourceLoweringPass: a SPIR-V-sourced
; `RWStructuredBuffer<float>` -- a bound `spirv.VulkanBuffer` handle over a
; flat (non-aggregate) element, the shape
; feme::spirv::StorageBufferAccessChainPattern produces (see
; feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-storage-buffer.mlir) --
; lowered directly into the same canonical `feme.cpu.resource.*` calls the
; DXIL `BoundResourceNormalizationPass`/`ResourceLoweringPass` pair produces,
; with (descriptor set 0, binding 1) assigned reserved heap slot 0 (the only
; accepted identity).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %idx, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define void @main(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
  ; CHECK: [[OFF:%.*]] = zext i32 %idx to i64
  ; CHECK: [[MUL:%.*]] = mul i64 [[OFF]], 4
  %v = load float, ptr %ptr
  ; CHECK: call float @feme.cpu.resource.load.raw.f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[MUL]], i1 true)
  %v2 = fadd float %v, 1.0
  store float %v2, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.raw.f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[MUL]], float %{{.*}}, i1 true)
  ret void
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK: !feme.cpu.resources = !{![[RMD:[0-9]+]]}
; CHECK: !feme.cpu.bound_resources = !{![[BMD:[0-9]+]]}
; CHECK: ![[RMD]] = !{!"main", i32 0, i1 false}
; CHECK: ![[BMD]] = !{!"main", i32 1, i32 0, i32 1, i32 1, i32 0}
