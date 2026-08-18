; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap step R26: an arrayed SPIR-V binding --
; `RWStructuredBuffer<float> Bufs[4] : register(u0, space0)`'s
; `llvm.spv.resource.handlefrombinding` with a range size greater than 1 and
; a dynamic array index -- is assigned a contiguous run of heap slots and
; range-checked exactly as feme::cpu::BoundResourceNormalizationPass does
; for a DXIL array binding, rather than the implicit single-slot range this
; pass assumed before R26.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %idx, i32 %which
define void @main(i32 %idx, i32 %which) {
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 4, i32 %which, ptr null)
  ; CHECK: [[OOR:%.*]] = icmp uge i32 %which, 4
  ; CHECK: [[SUM:%.*]] = add i64 0, [[ZEXT:%.*]]
  ; CHECK: [[OVF:%.*]] = icmp ugt i64 [[SUM]], 4294967295
  ; CHECK: [[CLAMPED:%.*]] = select i1 [[OVF]], i32 -1, i32 [[TRUNC:%.*]]
  ; CHECK: [[HEAPIDX:%.*]] = select i1 [[OOR]], i32 -1, i32 [[CLAMPED]]
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
  %v = load float, ptr %ptr
  ; CHECK: call float @feme.cpu.resource.load.raw.f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 [[HEAPIDX]], i64 {{.*}}, i1 true)
  %v2 = fadd float %v, 1.0
  store float %v2, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.raw.f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 [[HEAPIDX]], i64 {{.*}}, float %{{.*}}, i1 true)
  ret void
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK: !feme.cpu.bound_resources = !{![[BMD:[0-9]+]]}
; {name, prefix-size, (set, binding, range-size, heap-base)}: a range size
; of 4 is now recorded, rather than the implicit 1 this pass assigned every
; binding before R26.
; CHECK: ![[BMD]] = !{!"main", i32 4, i32 0, i32 0, i32 0, i32 1, i32 4, i32 0, i32 0}
