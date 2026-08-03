; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-raised -S %s | FileCheck %s

; feme::amdgpu::RaisedLoweringPass (feme/lib/Transforms/AMDGPU/RaisedLowering.cpp)
; rewrites the raised, format-agnostic thread/group index queries
; feme::dxil::OpRaisingPass produces (see test/Transforms/DXIL/dxil-raise-ops.ll)
; into the AMDGPU target intrinsics they correspond to, and moves local
; variables into AMDGPU's private address space, per the "Raised LLVM IR ->
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

; ThreadId (the dispatch-wide index) has no single AMDGPU intrinsic: it is the
; workgroup's id scaled by the thread group's size plus the workitem id, so
; lowering it needs the entry point's `hlsl.numthreads` dimensions.
; CHECK-LABEL: define i32 @thread_id_dispatch_wide(
define i32 @thread_id_dispatch_wide() #0 {
  ; CHECK: [[GROUP:%.*]] = call i32 @llvm.amdgcn.workgroup.id.y()
  ; CHECK: [[ITEM:%.*]] = call i32 @llvm.amdgcn.workitem.id.y()
  ; CHECK: [[SCALED:%.*]] = mul i32 [[GROUP]], 4
  ; CHECK: add i32 [[SCALED]], [[ITEM]]
  %1 = call i32 @llvm.dx.thread.id(i32 1)
  ret i32 %1
}

; The flattened thread id in group is the same linearization AMDGPU has no
; intrinsic for, over the workitem ids alone.
; CHECK-LABEL: define i32 @flattened_thread_id_in_group(
define i32 @flattened_thread_id_in_group() #0 {
  ; CHECK: [[X:%.*]] = call i32 @llvm.amdgcn.workitem.id.x()
  ; CHECK: [[Y:%.*]] = call i32 @llvm.amdgcn.workitem.id.y()
  ; CHECK: [[YS:%.*]] = mul i32 [[Y]], 8
  ; CHECK: [[XY:%.*]] = add i32 [[X]], [[YS]]
  ; CHECK: [[Z:%.*]] = call i32 @llvm.amdgcn.workitem.id.z()
  ; CHECK: [[ZS:%.*]] = mul i32 [[Z]], 32
  ; CHECK: add i32 [[XY]], [[ZS]]
  %1 = call i32 @llvm.dx.flattened.thread.id.in.group()
  ret i32 %1
}

; Without an `hlsl.numthreads` attribute there are no thread group dimensions
; to linearize against, so the call is left unmodified.
; CHECK-LABEL: define i32 @thread_id_no_numthreads(
define i32 @thread_id_no_numthreads() {
  ; CHECK: call i32 @llvm.dx.thread.id(i32 0)
  %1 = call i32 @llvm.dx.thread.id(i32 0)
  ret i32 %1
}

; A shader entry point becomes a real AMDGPU kernel: nothing can dispatch an
; ordinary device function.
; CHECK: define amdgpu_kernel void @entry() [[ATTRS:#[0-9]+]]
define void @entry() #1 {
  ret void
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

; CHECK-DAG: attributes [[ATTRS]] = {{{.*}}"amdgpu-flat-work-group-size"="1,32"{{.*}}}

attributes #0 = { "hlsl.numthreads"="8,4,1" }
attributes #1 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,4,1" }

declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.thread.id.in.group(i32)
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.flattened.thread.id.in.group()
