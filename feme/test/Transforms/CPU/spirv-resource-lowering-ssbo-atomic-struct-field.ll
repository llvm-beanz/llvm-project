; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap H8x: a direct-field storage block (`HandleKind::StorageStruct`,
; a bound `spirv.VulkanBuffer` over a plain struct rather than a runtime
; array -- GLSL's own `buffer SSBO { int a; int b; };` with no trailing
; array member) atomic against a *non-zero-offset* field, reached through
; `getpointer`'s own compile-time-constant field index with no further
; GEP needed (a scalar `int` field, unlike a struct-typed field which
; would need one) -- confirms the struct-member-offset case is already
; handled by `lowerRawPointerUses`'s pre-existing offset threading, with
; no further special-casing needed for an atomic specifically (see
; `lowerAccesses`'s own `StorageStruct` offset computation, reused
; unchanged from before this roadmap row, and
; spirv-resource-lowering-ssbo-atomic.ll's own `HandleKind::Storage`
; runtime-array atomic tests for the flat-buffer case this test
; complements).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @atomic_add_struct_field(
define i32 @atomic_add_struct_field(i32 %value) {
  %h = call target("spirv.VulkanBuffer", {i32, i32}, 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {i32, i32}, 12, 1) %h, i32 1)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.resource.atomic.add.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

declare target("spirv.VulkanBuffer", {i32, i32}, 12, 1)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {i32, i32}, 12, 1), i32)

; CHECK: !{!"atomic_add_struct_field", i32 0, i1 false, i32 0, i32 0, i32 0}
