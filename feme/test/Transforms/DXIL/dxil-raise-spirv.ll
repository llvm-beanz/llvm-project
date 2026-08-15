; RUN: feme-opt --llvm -passes=feme-dxil-raise-spirv -S %s | FileCheck %s

; feme::dxil::SPIRVRaisingPass (feme/lib/Transforms/DXIL/SPIRVRaising.cpp)
; rewrites SPIR-V-derived, format-specific IR back into the raised,
; format-agnostic `llvm.dx.*` conventions feme::dxil::OpRaisingPass's own
; output already uses, per the "SPIR-V -> DXIL direction" section of
; feme/docs/Roadmap.md. See test/Transforms/SPIRV/spirv-lower-raised.ll for
; the mirror-image (DXIL-derived -> SPIR-V) direction this inverts.

target triple = "dxil-unknown-shadermodel6.5-compute"

; CHECK-LABEL: define i32 @thread_id(
define i32 @thread_id() {
  ; CHECK: call i32 @llvm.dx.thread.id(i32 1)
  %1 = call i32 @llvm.spv.thread.id.i32(i32 1)
  ret i32 %1
}

; CHECK-LABEL: define i32 @group_id(
define i32 @group_id() {
  ; CHECK: call i32 @llvm.dx.group.id(i32 0)
  %1 = call i32 @llvm.spv.group.id.i32(i32 0)
  ret i32 %1
}

; CHECK-LABEL: define i32 @thread_id_in_group(
define i32 @thread_id_in_group() {
  ; CHECK: call i32 @llvm.dx.thread.id.in.group(i32 2)
  %1 = call i32 @llvm.spv.thread.id.in.group.i32(i32 2)
  ret i32 %1
}

; CHECK-LABEL: define i32 @flattened_thread_id_in_group(
define i32 @flattened_thread_id_in_group() {
  ; CHECK: call i32 @llvm.dx.flattened.thread.id.in.group()
  %1 = call i32 @llvm.spv.flattened.thread.id.in.group()
  ret i32 %1
}

; `llvm.spv.thread.id`'s overloaded return width is not `i32`, so there is no
; `llvm.dx.*` counterpart to raise into: left unmodified.
; CHECK-LABEL: define i64 @thread_id_wrong_width(
define i64 @thread_id_wrong_width() {
  ; CHECK: call i64 @llvm.spv.thread.id.i64(i32 0)
  %1 = call i64 @llvm.spv.thread.id.i64(i32 0)
  ret i64 %1
}

; A `StorageBuffer` block (`RWStructuredBuffer<float>`), accessed only
; through a flat `llvm.spv.resource.getpointer` plus an ordinary load/store,
; raises into DXIL's `dx.RawBuffer` handle and
; `llvm.dx.resource.load.rawbuffer`/`store.rawbuffer`.
; CHECK-LABEL: define void @storage_buffer_rw(
define void @storage_buffer_rw(i32 %idx) {
  ; CHECK: [[H:%.*]] = call target("dx.RawBuffer", float, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 0, i32 %idx, ptr null)
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 0, i32 %idx, ptr null)
  ; CHECK: [[LOADED:%.*]] = call { float, i1 } @llvm.dx.resource.load.rawbuffer{{.*}}(target("dx.RawBuffer", float, 1, 0) [[H]], i32 %idx, i32 0)
  ; CHECK: [[VAL:%.*]] = extractvalue { float, i1 } [[LOADED]], 0
  %ptr = call ptr addrspace(11) @llvm.spv.resource.getpointer(
      target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
  %v = load float, ptr addrspace(11) %ptr
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", float, 1, 0) [[H]], i32 %idx, i32 0, float [[VAL]])
  store float %v, ptr addrspace(11) %ptr
  ret void
}

; A read-only `StructuredBuffer<T>` (SRV, `IsWriteable` = 0) raises into a
; non-UAV `dx.RawBuffer` handle.
; CHECK-LABEL: define float @storage_buffer_readonly(
define float @storage_buffer_readonly(i32 %idx) {
  ; CHECK: call target("dx.RawBuffer", float, 0, 0) @llvm.dx.resource.handlefrombinding{{.*}}
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 0)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 0, i32 %idx, ptr null)
  %ptr = call ptr addrspace(11) @llvm.spv.resource.getpointer(
      target("spirv.VulkanBuffer", [0 x float], 12, 0) %h, i32 %idx)
  %v = load float, ptr addrspace(11) %ptr
  ret float %v
}

; A structured-buffer field access (a `getelementptr` off `getpointer`'s own
; result) is not one of the shapes this pass models (see the header
; comment's scope), so the handle is left unmodified entirely.
; CHECK-LABEL: define float @storage_buffer_field_access(
define float @storage_buffer_field_access(i32 %idx) {
  ; CHECK: call target("spirv.VulkanBuffer",{{.*}}) @llvm.spv.resource.handlefrombinding
  %h = call target("spirv.VulkanBuffer", [0 x {float, float}], 12, 0)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 0, i32 %idx, ptr null)
  %ptr = call ptr addrspace(11) @llvm.spv.resource.getpointer(
      target("spirv.VulkanBuffer", [0 x {float, float}], 12, 0) %h, i32 %idx)
  %field = getelementptr {float, float}, ptr addrspace(11) %ptr, i32 0, i32 1
  %v = load float, ptr addrspace(11) %field
  ret float %v
}

declare i32 @llvm.spv.thread.id.i32(i32)
declare i64 @llvm.spv.thread.id.i64(i32)
declare i32 @llvm.spv.group.id.i32(i32)
declare i32 @llvm.spv.thread.id.in.group.i32(i32)
declare i32 @llvm.spv.flattened.thread.id.in.group()
