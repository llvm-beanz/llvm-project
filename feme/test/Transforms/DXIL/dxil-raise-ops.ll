; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; feme::dxil::OpRaisingPass (feme/lib/Transforms/DXIL/OpRaising.cpp) rewrites
; calls to DXIL's `dx.op.*` functions back to the LLVM/`llvm.dx.*` intrinsic
; calls they were lowered from by LLVM's own `DXILOpLowering` pass -- see the
; "op raising" step under the DXIL section of feme/docs/Design.md. This test
; hand-writes already-lowered `dx.op.*` IR (the shape `DXILOpLowering`
; produces, see e.g. llvm/test/CodeGen/DirectX/sin.ll and
; llvm/test/CodeGen/DirectX/comput_ids.ll) for a representative sample of the
; opcodes this pass currently covers; dxil-raise-ops-roundtrip.ll separately
; validates this against real `-dxil-op-lower` output.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.7-compute"

; CHECK-LABEL: define float @sin_f32(
define float @sin_f32(float %a) {
  ; CHECK: call float @llvm.sin.f32(float %a)
  %1 = call float @dx.op.unary.f32(i32 13, float %a)
  ret float %1
}

; CHECK-LABEL: define float @sqrt_f32(
define float @sqrt_f32(float %a) {
  ; CHECK: call float @llvm.sqrt.f32(float %a)
  %1 = call float @dx.op.unary.f32(i32 24, float %a)
  ret float %1
}

; CHECK-LABEL: define float @rsqrt_f32(
define float @rsqrt_f32(float %a) {
  ; CHECK: call float @llvm.dx.rsqrt.f32(float %a)
  %1 = call float @dx.op.unary.f32(i32 25, float %a)
  ret float %1
}

; CHECK-LABEL: define float @saturate_f32(
define float @saturate_f32(float %a) {
  ; CHECK: call float @llvm.dx.saturate.f32(float %a)
  %1 = call float @dx.op.unary.f32(i32 7, float %a)
  ret float %1
}

; CHECK-LABEL: define i1 @isnan_f32(
define i1 @isnan_f32(float %a) {
  ; CHECK: call i1 @llvm.dx.isnan.f32(float %a)
  %1 = call i1 @dx.op.isSpecialFloat.f32(i32 8, float %a)
  ret i1 %1
}

; CHECK-LABEL: define i32 @thread_id(
define i32 @thread_id(i32 %a) {
  ; CHECK: call i32 @llvm.dx.thread.id(i32 %a)
  %1 = call i32 @dx.op.threadId.i32(i32 93, i32 %a)
  ret i32 %1
}

; CHECK-LABEL: define i32 @flattened_thread_id_in_group(
define i32 @flattened_thread_id_in_group() {
  ; CHECK: call i32 @llvm.dx.flattened.thread.id.in.group()
  %1 = call i32 @dx.op.flattenedThreadIdInGroup.i32(i32 96)
  ret i32 %1
}

; CHECK-LABEL: define i1 @wave_is_first_lane(
define i1 @wave_is_first_lane() {
  ; CHECK: call i1 @llvm.dx.wave.is.first.lane()
  %1 = call i1 @dx.op.waveIsFirstLane.i1(i32 110)
  ret i1 %1
}

; CHECK-LABEL: define i32 @countbits_i32(
define i32 @countbits_i32(i32 %a) {
  ; CHECK: call i32 @llvm.ctpop.i32(i32 %a)
  %1 = call i32 @dx.op.unaryBits.i32(i32 31, i32 %a)
  ret i32 %1
}

; CHECK-LABEL: define i32 @firstbitlow_i32(
define i32 @firstbitlow_i32(i32 %a) {
  ; CHECK: call i32 @llvm.dx.firstbitlow.i32(i32 %a)
  %1 = call i32 @dx.op.unaryBits.i32(i32 32, i32 %a)
  ret i32 %1
}

; CHECK-LABEL: define float @fmax_f32(
define float @fmax_f32(float %a, float %b) {
  ; CHECK: call float @llvm.maxnum.f32(float %a, float %b)
  %1 = call float @dx.op.binary.f32(i32 35, float %a, float %b)
  ret float %1
}

; CHECK-LABEL: define i32 @smin_i32(
define i32 @smin_i32(i32 %a, i32 %b) {
  ; CHECK: call i32 @llvm.smin.i32(i32 %a, i32 %b)
  %1 = call i32 @dx.op.binary.i32(i32 38, i32 %a, i32 %b)
  ret i32 %1
}

; CHECK-LABEL: define i32 @umax_i32(
define i32 @umax_i32(i32 %a, i32 %b) {
  ; CHECK: call i32 @llvm.umax.i32(i32 %a, i32 %b)
  %1 = call i32 @dx.op.binary.i32(i32 39, i32 %a, i32 %b)
  ret i32 %1
}

; CHECK-LABEL: define float @fmad_f32(
define float @fmad_f32(float %a, float %b, float %c) {
  ; CHECK: call float @llvm.fmuladd.f32(float %a, float %b, float %c)
  %1 = call float @dx.op.tertiary.f32(i32 46, float %a, float %b, float %c)
  ret float %1
}

; CHECK-LABEL: define i32 @imad_i32(
define i32 @imad_i32(i32 %a, i32 %b, i32 %c) {
  ; CHECK: call i32 @llvm.dx.imad.i32(i32 %a, i32 %b, i32 %c)
  %1 = call i32 @dx.op.tertiary.i32(i32 48, i32 %a, i32 %b, i32 %c)
  ret i32 %1
}

; CHECK-LABEL: define float @dot3_f32(
define float @dot3_f32(float %ax, float %ay, float %az, float %bx, float %by, float %bz) {
  ; CHECK: call float @llvm.dx.dot3.f32(float %ax, float %ay, float %az, float %bx, float %by, float %bz)
  %1 = call float @dx.op.dot3.f32(i32 55, float %ax, float %ay, float %az, float %bx, float %by, float %bz)
  ret float %1
}

; SM6.9's unified `FDot` op (opcode 311) unlike Dot2..Dot4 above takes its
; two operand vectors directly rather than 2*N interleaved scalars (see the
; comment on the `{311, ...}` DirectOps row in OpRaising.cpp) -- e.g. the
; `dot(half2, half2)`/`dot(half3, half3)` calls `dxc -T cs_6_9` emits as
; `dx.op.dot.v2f16`/`dx.op.dot.v3f16`.
; CHECK-LABEL: define half @fdot_v2f16(
define half @fdot_v2f16(<2 x half> %a, <2 x half> %b) {
  ; CHECK: call half @llvm.dx.fdot.v2f16(<2 x half> %a, <2 x half> %b)
  %1 = call half @dx.op.dot.v2f16(i32 311, <2 x half> %a, <2 x half> %b)
  ret half %1
}

; CHECK-LABEL: define float @deriv_coarse_x_f32(
define float @deriv_coarse_x_f32(float %a) {
  ; CHECK: call float @llvm.dx.ddx.coarse.f32(float %a)
  %1 = call float @dx.op.unary.f32(i32 83, float %a)
  ret float %1
}

; CHECK-LABEL: define double @make_double(
define double @make_double(i32 %lo, i32 %hi) {
  ; CHECK: call double @llvm.dx.asdouble.i32(i32 %lo, i32 %hi)
  %1 = call double @dx.op.makeDouble.f64(i32 101, i32 %lo, i32 %hi)
  ret double %1
}

; CHECK-LABEL: define i1 @wave_active_all_equal_i32(
define i1 @wave_active_all_equal_i32(i32 %a) {
  ; CHECK: call i1 @llvm.dx.wave.all.equal.i32(i32 %a)
  %1 = call i1 @dx.op.waveActiveAllEqual.i32(i32 115, i32 %a)
  ret i1 %1
}

; CHECK-LABEL: define i32 @wave_read_lane_at_i32(
define i32 @wave_read_lane_at_i32(i32 %a, i32 %lane) {
  ; CHECK: call i32 @llvm.dx.wave.readlane.i32(i32 %a, i32 %lane)
  %1 = call i32 @dx.op.waveReadLaneAt.i32(i32 117, i32 %a, i32 %lane)
  ret i32 %1
}

; CHECK-LABEL: define i32 @legacy_f32_to_f16(
define i32 @legacy_f32_to_f16(float %a) {
  ; CHECK: call i32 @llvm.dx.legacyf32tof16.f32(float %a)
  %1 = call i32 @dx.op.legacyF32ToF16(i32 130, float %a)
  ret i32 %1
}

; CHECK-LABEL: define i32 @wave_all_bit_count(
define i32 @wave_all_bit_count(i1 %a) {
  ; CHECK: call i32 @llvm.dx.wave.active.countbits(i1 %a)
  %1 = call i32 @dx.op.waveAllOp(i32 135, i1 %a)
  ret i32 %1
}

; CHECK-LABEL: define void @discard(
define void @discard(i1 %a) {
  ; CHECK: call void @llvm.dx.discard(i1 %a)
  call void @dx.op.discard(i32 82, i1 %a)
  ret void
}

; CHECK-LABEL: define i32 @dot4_add_i8_packed(
define i32 @dot4_add_i8_packed(i32 %acc, i32 %a, i32 %b) {
  ; CHECK: call i32 @llvm.dx.dot4add.i8packed(i32 %acc, i32 %a, i32 %b)
  %1 = call i32 @dx.op.dot4AddPacked.i32(i32 163, i32 %acc, i32 %a, i32 %b)
  ret i32 %1
}

; DXILOpLowering lowers both `IsFinite` and `IsNormal` from the generic
; `llvm.is.fpclass` intrinsic, picking the DXIL opcode via `is.fpclass`'s
; `FPClassTest` mask operand (`fcFinite` / `fcNormal`) rather than via a
; dedicated per-op intrinsic -- see the `raiseIsFPClassCall` comment in
; OpRaising.cpp. `fcFinite` is 504, `fcNormal` is 264 (`FloatingPointMode.h`).
; CHECK-LABEL: define i1 @is_finite_f32(
define i1 @is_finite_f32(float %a) {
  ; CHECK: call i1 @llvm.is.fpclass.f32(float %a, {{.*}}i32 504)
  %1 = call i1 @dx.op.isSpecialFloat.f32(i32 10, float %a)
  ret i1 %1
}

; CHECK-LABEL: define i1 @is_normal_f32(
define i1 @is_normal_f32(float %a) {
  ; CHECK: call i1 @llvm.is.fpclass.f32(float %a, {{.*}}i32 264)
  %1 = call i1 @dx.op.isSpecialFloat.f32(i32 11, float %a)
  ret i1 %1
}

; An opcode this pass does not (yet) cover -- a resource-handle op, left for
; a later change per feme/docs/Design.md -- must be left as an unmodified
; `dx.op.*` call rather than erroring, since op raising is expected to grow
; incrementally.
; CHECK-LABEL: define %dx.types.Handle @unhandled_opcode(
define %dx.types.Handle @unhandled_opcode(i8 %class, i32 %rangeId, i32 %index, i1 %nonUniform) {
  ; CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57, i8 %class, i32 %rangeId, i32 %index, i1 %nonUniform)
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 %class, i32 %rangeId, i32 %index, i1 %nonUniform)
  ret %dx.types.Handle %1
}

%dx.types.Handle = type { ptr }

declare float @dx.op.unary.f32(i32, float)
declare i1 @dx.op.isSpecialFloat.f32(i32, float)
declare i32 @dx.op.threadId.i32(i32, i32)
declare i32 @dx.op.flattenedThreadIdInGroup.i32(i32)
declare i1 @dx.op.waveIsFirstLane.i1(i32)
declare float @dx.op.binary.f32(i32, float, float)
declare i32 @dx.op.unaryBits.i32(i32, i32)
declare i32 @dx.op.binary.i32(i32, i32, i32)
declare float @dx.op.tertiary.f32(i32, float, float, float)
declare i32 @dx.op.tertiary.i32(i32, i32, i32, i32)
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float)
declare half @dx.op.dot.v2f16(i32, <2 x half>, <2 x half>)
declare double @dx.op.makeDouble.f64(i32, i32, i32)
declare i1 @dx.op.waveActiveAllEqual.i32(i32, i32)
declare i32 @dx.op.waveReadLaneAt.i32(i32, i32, i32)
declare i32 @dx.op.legacyF32ToF16(i32, float)
declare i32 @dx.op.waveAllOp(i32, i1)
declare void @dx.op.discard(i32, i1)
declare i32 @dx.op.dot4AddPacked.i32(i32, i32, i32, i32)
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
