; RUN: feme-opt --llvm -passes=feme-nvptx-lower-raised -S %s | FileCheck %s

; feme::nvptx::RaisedLoweringPass (feme/lib/Transforms/NVPTX/RaisedLowering.cpp)
; rewrites the raised, format-agnostic thread/group index queries
; feme::dxil::OpRaisingPass produces (see test/Transforms/DXIL/dxil-raise-ops.ll)
; into the NVPTX/NVVM target intrinsics they correspond to, and moves local
; variables into NVPTX's local address space -- the NVPTX counterpart to
; test/Transforms/AMDGPU/amdgpu-lower-raised.ll, which this mirrors (see
; Roadmap.md's "Retargeting" section).

target triple = "nvptx64-nvidia-cuda"

; CHECK-LABEL: define i32 @group_id_x(
define i32 @group_id_x() {
  ; CHECK: call i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
  %1 = call i32 @llvm.dx.group.id(i32 0)
  ret i32 %1
}

; CHECK-LABEL: define i32 @group_id_y(
define i32 @group_id_y() {
  ; CHECK: call i32 @llvm.nvvm.read.ptx.sreg.ctaid.y()
  %1 = call i32 @llvm.dx.group.id(i32 1)
  ret i32 %1
}

; CHECK-LABEL: define i32 @group_id_z(
define i32 @group_id_z() {
  ; CHECK: call i32 @llvm.nvvm.read.ptx.sreg.ctaid.z()
  %1 = call i32 @llvm.dx.group.id(i32 2)
  ret i32 %1
}

; CHECK-LABEL: define i32 @thread_id_in_group_x(
define i32 @thread_id_in_group_x() {
  ; CHECK: call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  %1 = call i32 @llvm.dx.thread.id.in.group(i32 0)
  ret i32 %1
}

; ThreadId (the dispatch-wide index) has no single NVPTX intrinsic: it is the
; block's id scaled by the thread group's size plus the in-block thread id,
; so lowering it needs the entry point's `hlsl.numthreads` dimensions.
; CHECK-LABEL: define i32 @thread_id_dispatch_wide(
define i32 @thread_id_dispatch_wide() #0 {
  ; CHECK: [[BLOCK:%.*]] = call i32 @llvm.nvvm.read.ptx.sreg.ctaid.y()
  ; CHECK: [[ITEM:%.*]] = call i32 @llvm.nvvm.read.ptx.sreg.tid.y()
  ; CHECK: [[SCALED:%.*]] = mul i32 [[BLOCK]], 4
  ; CHECK: add i32 [[SCALED]], [[ITEM]]
  %1 = call i32 @llvm.dx.thread.id(i32 1)
  ret i32 %1
}

; The flattened thread id in group is the same linearization NVPTX has no
; intrinsic for, over the in-block thread ids alone.
; CHECK-LABEL: define i32 @flattened_thread_id_in_group(
define i32 @flattened_thread_id_in_group() #0 {
  ; CHECK: [[X:%.*]] = call i32 @llvm.nvvm.read.ptx.sreg.tid.x()
  ; CHECK: [[Y:%.*]] = call i32 @llvm.nvvm.read.ptx.sreg.tid.y()
  ; CHECK: [[YS:%.*]] = mul i32 [[Y]], 8
  ; CHECK: [[XY:%.*]] = add i32 [[X]], [[YS]]
  ; CHECK: [[Z:%.*]] = call i32 @llvm.nvvm.read.ptx.sreg.tid.z()
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

; A shader entry point becomes a real PTX kernel: nothing can launch an
; ordinary device function.
; CHECK: define ptx_kernel void @entry()
define void @entry() #1 {
  ret void
}

; A non-constant component operand cannot be mapped to a single
; per-component NVPTX intrinsic, so it is left unmodified too.
; CHECK-LABEL: define i32 @group_id_dynamic_component(
define i32 @group_id_dynamic_component(i32 %component) {
  ; CHECK: call i32 @llvm.dx.group.id(i32 %component)
  %1 = call i32 @llvm.dx.group.id(i32 %component)
  ret i32 %1
}

; An out-of-range constant component (DXIL/NVPTX components are 0/1/2 for
; x/y/z) is left unmodified rather than indexing past the mapping table.
; CHECK-LABEL: define i32 @group_id_out_of_range_component(
define i32 @group_id_out_of_range_component() {
  ; CHECK: call i32 @llvm.dx.group.id(i32 3)
  %1 = call i32 @llvm.dx.group.id(i32 3)
  ret i32 %1
}

attributes #0 = { "hlsl.numthreads"="8,4,1" }
attributes #1 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,4,1" }

declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.thread.id.in.group(i32)
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.flattened.thread.id.in.group()
