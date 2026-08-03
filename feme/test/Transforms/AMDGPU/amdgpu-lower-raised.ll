; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-raised -S %s | FileCheck %s

; feme::amdgpu::RaisedLoweringPass (feme/lib/Transforms/AMDGPU/RaisedLowering.cpp)
; rewrites the raised, format-agnostic thread/group index queries
; feme::dxil::OpRaisingPass produces (see test/Transforms/DXIL/dxil-raise-ops.ll)
; into the AMDGPU target intrinsics they correspond to, per the "Raised LLVM IR ->
; AMDGPU" section of feme/docs/Design.md.

target triple = "amdgcn-amd-amdhsa"

; CHECK-LABEL: define i32 @group_id_x(
define i32 @group_id_x() {
  ; CHECK: call i32 @llvm.amdgcn.workgroup.id.x()
  %1 = call i32 @llvm.dx.group.id(i32 0)
  ret i32 %1
}

; CHECK-LABEL: define i32 @group_id_y(
define i32 @group_id_y() {
  ; CHECK: call i32 @llvm.amdgcn.workgroup.id.y()
  %1 = call i32 @llvm.dx.group.id(i32 1)
  ret i32 %1
}

; CHECK-LABEL: define i32 @group_id_z(
define i32 @group_id_z() {
  ; CHECK: call i32 @llvm.amdgcn.workgroup.id.z()
  %1 = call i32 @llvm.dx.group.id(i32 2)
  ret i32 %1
}

; CHECK-LABEL: define i32 @thread_id_in_group_x(
define i32 @thread_id_in_group_x() {
  ; CHECK: call i32 @llvm.amdgcn.workitem.id.x()
  %1 = call i32 @llvm.dx.thread.id.in.group(i32 0)
  ret i32 %1
}

; ThreadId (the dispatch-wide index) is not yet lowered: it needs the
; workgroup's id and dimensions combined with the workitem id, not a single
; AMDGPU intrinsic call, so it is left unmodified.
; CHECK-LABEL: define i32 @thread_id_dispatch_wide(
define i32 @thread_id_dispatch_wide() {
  ; CHECK: call i32 @llvm.dx.thread.id(i32 0)
  %1 = call i32 @llvm.dx.thread.id(i32 0)
  ret i32 %1
}

; A non-constant component operand cannot be mapped to a single
; per-component AMDGPU intrinsic, so it is left unmodified too.
; CHECK-LABEL: define i32 @group_id_dynamic_component(
define i32 @group_id_dynamic_component(i32 %component) {
  ; CHECK: call i32 @llvm.dx.group.id(i32 %component)
  %1 = call i32 @llvm.dx.group.id(i32 %component)
  ret i32 %1
}

; An out-of-range constant component (DXIL/AMDGPU components are 0/1/2 for
; x/y/z) is left unmodified rather than indexing past the mapping table.
; CHECK-LABEL: define i32 @group_id_out_of_range_component(
define i32 @group_id_out_of_range_component() {
  ; CHECK: call i32 @llvm.dx.group.id(i32 3)
  %1 = call i32 @llvm.dx.group.id(i32 3)
  ret i32 %1
}

declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.thread.id.in.group(i32)
declare i32 @llvm.dx.thread.id(i32)
