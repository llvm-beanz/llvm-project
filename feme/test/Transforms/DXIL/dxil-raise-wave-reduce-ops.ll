; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's raising of `WaveActiveOp` (opcode 119),
; `WaveActiveBit` (opcode 120) and `WavePrefixOp` (opcode 121)
; (raiseReduceOpCall/RaisableReduceOp and raiseWaveActiveBitCall/
; RaisableBitOp in OpRaising.cpp): each selects its source intrinsic from a
; constant flag operand (or pair of them) rather than the opcode alone --
; roadmap step R4 in feme/docs/Roadmap.md.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define i32 @wave_active_sum(
define i32 @wave_active_sum(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.sum.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %v, i8 0, i8 0)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_active_usum(
define i32 @wave_active_usum(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.usum.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %v, i8 0, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define float @wave_active_product(
define float @wave_active_product(float %v) {
  ; CHECK: call float @llvm.dx.wave.product.f32(float %v)
  %r = call float @dx.op.waveActiveOp.f32(i32 119, float %v, i8 1, i8 0)
  ret float %r
}

; CHECK-LABEL: define i32 @wave_active_uproduct(
define i32 @wave_active_uproduct(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.uproduct.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %v, i8 1, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define float @wave_active_max(
define float @wave_active_max(float %v) {
  ; CHECK: call float @llvm.dx.wave.reduce.max.f32(float %v)
  %r = call float @dx.op.waveActiveOp.f32(i32 119, float %v, i8 3, i8 0)
  ret float %r
}

; CHECK-LABEL: define i32 @wave_active_umax(
define i32 @wave_active_umax(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.umax.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %v, i8 3, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define float @wave_active_min(
define float @wave_active_min(float %v) {
  ; CHECK: call float @llvm.dx.wave.reduce.min.f32(float %v)
  %r = call float @dx.op.waveActiveOp.f32(i32 119, float %v, i8 2, i8 0)
  ret float %r
}

; CHECK-LABEL: define i32 @wave_active_umin(
define i32 @wave_active_umin(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.umin.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %v, i8 2, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_active_bitand(
define i32 @wave_active_bitand(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.and.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveBit.i32(i32 120, i32 %v, i8 0)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_active_bitor(
define i32 @wave_active_bitor(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.or.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveBit.i32(i32 120, i32 %v, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_active_bitxor(
define i32 @wave_active_bitxor(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.reduce.xor.i32(i32 %v)
  %r = call i32 @dx.op.waveActiveBit.i32(i32 120, i32 %v, i8 2)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_prefix_sum(
define i32 @wave_prefix_sum(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.prefix.sum.i32(i32 %v)
  %r = call i32 @dx.op.wavePrefixOp.i32(i32 121, i32 %v, i8 0, i8 0)
  ret i32 %r
}

; CHECK-LABEL: define i32 @wave_prefix_usum(
define i32 @wave_prefix_usum(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.prefix.usum.i32(i32 %v)
  %r = call i32 @dx.op.wavePrefixOp.i32(i32 121, i32 %v, i8 0, i8 1)
  ret i32 %r
}

; CHECK-LABEL: define float @wave_prefix_product(
define float @wave_prefix_product(float %v) {
  ; CHECK: call float @llvm.dx.wave.prefix.product.f32(float %v)
  %r = call float @dx.op.wavePrefixOp.f32(i32 121, float %v, i8 1, i8 0)
  ret float %r
}

; CHECK-LABEL: define i32 @wave_prefix_uproduct(
define i32 @wave_prefix_uproduct(i32 %v) {
  ; CHECK: call i32 @llvm.dx.wave.prefix.uproduct.i32(i32 %v)
  %r = call i32 @dx.op.wavePrefixOp.i32(i32 121, i32 %v, i8 1, i8 1)
  ret i32 %r
}

; WavePrefixOp has no Min/Max flag combination (DXIL never emits one); an
; unrecognized flag pair must be left as an unmodified `dx.op.*` call rather
; than erroring, matching every other raiser's treatment of unrecognized
; shapes.
; CHECK-LABEL: define i32 @wave_prefix_unrecognized(
define i32 @wave_prefix_unrecognized(i32 %v) {
  ; CHECK: call i32 @dx.op.wavePrefixOp.i32(i32 121, i32 %v, i8 3, i8 0)
  %r = call i32 @dx.op.wavePrefixOp.i32(i32 121, i32 %v, i8 3, i8 0)
  ret i32 %r
}

declare i32 @dx.op.waveActiveOp.i32(i32, i32, i8, i8)
declare float @dx.op.waveActiveOp.f32(i32, float, i8, i8)
declare i32 @dx.op.waveActiveBit.i32(i32, i32, i8)
declare i32 @dx.op.wavePrefixOp.i32(i32, i32, i8, i8)
declare float @dx.op.wavePrefixOp.f32(i32, float, i8, i8)
