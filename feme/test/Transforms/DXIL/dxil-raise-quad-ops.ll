; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's raising of `QuadOp` (opcode 123;
; raiseQuadOpCall/RaisableQuadOp in OpRaising.cpp): it selects its source
; intrinsic from a constant direction flag operand rather than the opcode
; alone -- roadmap step R4 in feme/docs/Roadmap.md. Only the raising is
; covered here: the FeMe CPU target does not lower the resulting
; `llvm.dx.quad.read.*` calls yet (an explicit v1 non-goal, see
; feme/docs/FeMeCPUDesign.md's "Non-Goals").

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define i32 @quad_read_across_x(
define i32 @quad_read_across_x(i32 %v) {
  ; CHECK: call i32 @llvm.dx.quad.read.across.x.i32(i32 %v)
  %r = call i32 @dx.op.quadOp.i32(i32 123, i32 %v, i8 0)
  ret i32 %r
}

; CHECK-LABEL: define float @quad_read_across_y(
define float @quad_read_across_y(float %v) {
  ; CHECK: call float @llvm.dx.quad.read.across.y.f32(float %v)
  %r = call float @dx.op.quadOp.f32(i32 123, float %v, i8 1)
  ret float %r
}

; CHECK-LABEL: define i32 @quad_read_across_diagonal(
define i32 @quad_read_across_diagonal(i32 %v) {
  ; CHECK: call i32 @llvm.dx.quad.read.across.diagonal.i32(i32 %v)
  %r = call i32 @dx.op.quadOp.i32(i32 123, i32 %v, i8 2)
  ret i32 %r
}

; An unrecognized direction flag must be left as an unmodified `dx.op.*`
; call rather than erroring.
; CHECK-LABEL: define i32 @quad_op_unrecognized(
define i32 @quad_op_unrecognized(i32 %v) {
  ; CHECK: call i32 @dx.op.quadOp.i32(i32 123, i32 %v, i8 5)
  %r = call i32 @dx.op.quadOp.i32(i32 123, i32 %v, i8 5)
  ret i32 %r
}

declare i32 @dx.op.quadOp.i32(i32, i32, i8)
declare float @dx.op.quadOp.f32(i32, float, i8)
