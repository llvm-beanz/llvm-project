; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's Barrier raising (raiseBarrierCall in
; OpRaising.cpp): a `dx.op.barrier` (opcode 80) call is rewritten into one of
; the six `llvm.dx.*_memory_barrier[_with_group_sync]` intrinsic calls,
; selected via its constant mode operand (see `RaisableBarriers`) rather than
; the opcode alone. Barrier is a required raised operation for the FeMe CPU
; target (see feme/docs/FeMeCPUDesign.md's "Raised IR prerequisites").

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @device_memory_barrier(
define void @device_memory_barrier() {
  ; CHECK: call void @llvm.dx.device.memory.barrier()
  call void @dx.op.barrier(i32 80, i32 2)
  ret void
}

; CHECK-LABEL: define void @device_memory_barrier_with_group_sync(
define void @device_memory_barrier_with_group_sync() {
  ; CHECK: call void @llvm.dx.device.memory.barrier.with.group.sync()
  call void @dx.op.barrier(i32 80, i32 3)
  ret void
}

; CHECK-LABEL: define void @group_memory_barrier(
define void @group_memory_barrier() {
  ; CHECK: call void @llvm.dx.group.memory.barrier()
  call void @dx.op.barrier(i32 80, i32 8)
  ret void
}

; CHECK-LABEL: define void @group_memory_barrier_with_group_sync(
define void @group_memory_barrier_with_group_sync() {
  ; CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync()
  call void @dx.op.barrier(i32 80, i32 9)
  ret void
}

; CHECK-LABEL: define void @all_memory_barrier(
define void @all_memory_barrier() {
  ; CHECK: call void @llvm.dx.all.memory.barrier()
  call void @dx.op.barrier(i32 80, i32 10)
  ret void
}

; CHECK-LABEL: define void @all_memory_barrier_with_group_sync(
define void @all_memory_barrier_with_group_sync() {
  ; CHECK: call void @llvm.dx.all.memory.barrier.with.group.sync()
  call void @dx.op.barrier(i32 80, i32 11)
  ret void
}

; A mode value not in the table (e.g. 0, no barrier semantics at all) must be
; left as an unmodified `dx.op.*` call rather than erroring.
; CHECK-LABEL: define void @unrecognized_mode(
define void @unrecognized_mode() {
  ; CHECK: call void @dx.op.barrier(i32 80, i32 0)
  call void @dx.op.barrier(i32 80, i32 0)
  ret void
}

declare void @dx.op.barrier(i32, i32)
