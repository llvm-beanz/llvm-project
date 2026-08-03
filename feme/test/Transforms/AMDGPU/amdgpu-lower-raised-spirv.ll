; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-raised -S %s | FileCheck %s

; feme::amdgpu::RaisedLoweringPass (feme/lib/Transforms/AMDGPU/RaisedLowering.cpp)
; also matches the `llvm.spv.*` half of each raised, format-agnostic
; thread/group index query -- the spelling feme::SPIRVToLLVMTranslator
; produces for SPIR-V input (see test/Conversion/SPIRVToLLVM/*.mlir) -- since
; the two intrinsic families are parallel by construction. See the
; "Raised LLVM IR -> AMDGPU" section of feme/docs/Design.md and
; amdgpu-lower-raised.ll for the equivalent `llvm.dx.*` coverage.

target triple = "amdgcn-amd-amdhsa"

; CHECK-LABEL: define i32 @group_id_x(
define i32 @group_id_x() {
  ; CHECK: call i32 @llvm.amdgcn.workgroup.id.x()
  %1 = call i32 @llvm.spv.group.id.i32(i32 0)
  ret i32 %1
}

; CHECK-LABEL: define i32 @thread_id_in_group_y(
define i32 @thread_id_in_group_y() {
  ; CHECK: call i32 @llvm.amdgcn.workitem.id.y()
  %1 = call i32 @llvm.spv.thread.id.in.group.i32(i32 1)
  ret i32 %1
}

; ThreadId (the dispatch-wide index) has no single AMDGPU intrinsic: it is the
; workgroup's id scaled by the thread group's size plus the workitem id, so
; lowering it needs the entry point's `hlsl.numthreads` dimensions.
; CHECK-LABEL: define i32 @thread_id_dispatch_wide(
define i32 @thread_id_dispatch_wide() #0 {
  ; CHECK: [[GROUP:%.*]] = call i32 @llvm.amdgcn.workgroup.id.z()
  ; CHECK: [[ITEM:%.*]] = call i32 @llvm.amdgcn.workitem.id.z()
  ; CHECK: [[SCALED:%.*]] = mul i32 [[GROUP]], 1
  ; CHECK: add i32 [[SCALED]], [[ITEM]]
  %1 = call i32 @llvm.spv.thread.id.i32(i32 2)
  ret i32 %1
}

; The flattened thread id in group is the same linearization AMDGPU has no
; intrinsic for, over the workitem ids alone. Unlike the other three queries,
; `llvm.spv.flattened.thread.id.in.group` is not overloaded on width (see
; IntrinsicsSPIRV.td), so it is spelled identically to its `llvm.dx.*`
; counterpart.
; CHECK-LABEL: define i32 @flattened_thread_id_in_group(
define i32 @flattened_thread_id_in_group() #0 {
  ; CHECK: [[X:%.*]] = call i32 @llvm.amdgcn.workitem.id.x()
  ; CHECK: [[Y:%.*]] = call i32 @llvm.amdgcn.workitem.id.y()
  ; CHECK: [[YS:%.*]] = mul i32 [[Y]], 8
  ; CHECK: [[XY:%.*]] = add i32 [[X]], [[YS]]
  ; CHECK: [[Z:%.*]] = call i32 @llvm.amdgcn.workitem.id.z()
  ; CHECK: [[ZS:%.*]] = mul i32 [[Z]], 32
  ; CHECK: add i32 [[XY]], [[ZS]]
  %1 = call i32 @llvm.spv.flattened.thread.id.in.group()
  ret i32 %1
}

; A shader entry point becomes a real AMDGPU kernel regardless of which
; format raised it there: `lowerEntryPoint` keys off the format-agnostic
; `hlsl.shader`/`hlsl.numthreads` attributes alone.
; CHECK: define amdgpu_kernel void @entry() [[ATTRS:#[0-9]+]]
define void @entry() #1 {
  ret void
}

; `llvm.spv.group.id`/`llvm.spv.thread.id.in.group`/`llvm.spv.thread.id` are
; overloaded on return width (unlike their fixed-`i32` `llvm.dx.*`
; counterparts); a call instantiated at a width other than `i32` cannot be
; expressed as a 1:1 AMDGPU intrinsic call, so it is left unmodified.
; CHECK-LABEL: define i64 @group_id_non_i32_width(
define i64 @group_id_non_i32_width() {
  ; CHECK: call i64 @llvm.spv.group.id.i64(i32 0)
  %1 = call i64 @llvm.spv.group.id.i64(i32 0)
  ret i64 %1
}

; CHECK-DAG: attributes [[ATTRS]] = {{{.*}}"amdgpu-flat-work-group-size"="1,32"{{.*}}}

attributes #0 = { "hlsl.numthreads"="8,4,1" }
attributes #1 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,4,1" }

declare i32 @llvm.spv.group.id.i32(i32)
declare i64 @llvm.spv.group.id.i64(i32)
declare i32 @llvm.spv.thread.id.in.group.i32(i32)
declare i32 @llvm.spv.thread.id.i32(i32)
declare i32 @llvm.spv.flattened.thread.id.in.group()
