; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops -S | FileCheck %s

; End-to-end validation that feme::dxil::OpRaisingPass is a genuine inverse
; of LLVM's own `DXILOpLowering` pass (llvm/lib/Target/DirectX/DXILOpLowering.cpp),
; not just of hand-written `dx.op.*` IR matching this pass's own assumptions:
; this starts from the pre-lowering `llvm.*`/`llvm.dx.*` intrinsic calls a
; real DXIL-targeting frontend would emit, lowers them with the real
; `-dxil-op-lower` pass, then raises the result back and checks it matches
; the original intrinsic calls. See the DXIL section of feme/docs/Design.md.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.7-compute"

; CHECK-LABEL: define float @sin_f32(
define float @sin_f32(float %a) {
  ; CHECK: call float @llvm.sin.f32(float %a)
  %1 = call float @llvm.sin.f32(float %a)
  ret float %1
}

; CHECK-LABEL: define float @frac_f32(
define float @frac_f32(float %a) {
  ; CHECK: call float @llvm.dx.frac.f32(float %a)
  %1 = call float @llvm.dx.frac.f32(float %a)
  ret float %1
}

; CHECK-LABEL: define i32 @group_id(
define i32 @group_id(i32 %a) {
  ; CHECK: call i32 @llvm.dx.group.id(i32 %a)
  %1 = call i32 @llvm.dx.group.id(i32 %a)
  ret i32 %1
}

; CHECK-LABEL: define i32 @wave_get_lane_index(
define i32 @wave_get_lane_index() {
  ; CHECK: call i32 @llvm.dx.wave.getlaneindex()
  %1 = call i32 @llvm.dx.wave.getlaneindex()
  ret i32 %1
}

; CHECK-LABEL: define i32 @wave_get_lane_count(
define i32 @wave_get_lane_count() {
  ; CHECK: call i32 @llvm.dx.wave.get.lane.count()
  %1 = call i32 @llvm.dx.wave.get.lane.count()
  ret i32 %1
}

; CHECK-LABEL: define i32 @countbits_i32(
define i32 @countbits_i32(i32 %a) {
  ; CHECK: call i32 @llvm.ctpop.i32(i32 %a)
  %1 = call i32 @llvm.ctpop.i32(i32 %a)
  ret i32 %1
}

; CHECK-LABEL: define float @fmax_f32(
define float @fmax_f32(float %a, float %b) {
  ; CHECK: call float @llvm.maxnum.f32(float %a, float %b)
  %1 = call float @llvm.maxnum.f32(float %a, float %b)
  ret float %1
}

; CHECK-LABEL: define i32 @umin_i32(
define i32 @umin_i32(i32 %a, i32 %b) {
  ; CHECK: call i32 @llvm.umin.i32(i32 %a, i32 %b)
  %1 = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  ret i32 %1
}

; CHECK-LABEL: define float @fmad_f32(
define float @fmad_f32(float %a, float %b, float %c) {
  ; CHECK: call float @llvm.fmuladd.f32(float %a, float %b, float %c)
  %1 = call float @llvm.fmuladd.f32(float %a, float %b, float %c)
  ret float %1
}

; CHECK-LABEL: define i32 @umad_i32(
define i32 @umad_i32(i32 %a, i32 %b, i32 %c) {
  ; CHECK: call i32 @llvm.dx.umad.i32(i32 %a, i32 %b, i32 %c)
  %1 = call i32 @llvm.dx.umad.i32(i32 %a, i32 %b, i32 %c)
  ret i32 %1
}

; CHECK-LABEL: define float @dot2_f32(
define float @dot2_f32(float %ax, float %ay, float %bx, float %by) {
  ; CHECK: call float @llvm.dx.dot2.f32(float %ax, float %ay, float %bx, float %by)
  %1 = call float @llvm.dx.dot2.f32(float %ax, float %ay, float %bx, float %by)
  ret float %1
}

; CHECK-LABEL: define double @asdouble(
define double @asdouble(i32 %lo, i32 %hi) {
  ; CHECK: call double @llvm.dx.asdouble.i32(i32 %lo, i32 %hi)
  %1 = call double @llvm.dx.asdouble.i32(i32 %lo, i32 %hi)
  ret double %1
}

; CHECK-LABEL: define i1 @wave_all_equal_i32(
define i1 @wave_all_equal_i32(i32 %a) {
  ; CHECK: call i1 @llvm.dx.wave.all.equal.i32(i32 %a)
  %1 = call i1 @llvm.dx.wave.all.equal.i32(i32 %a)
  ret i1 %1
}

; CHECK-LABEL: define i32 @wave_readlane_i32(
define i32 @wave_readlane_i32(i32 %a, i32 %lane) {
  ; CHECK: call i32 @llvm.dx.wave.readlane.i32(i32 %a, i32 %lane)
  %1 = call i32 @llvm.dx.wave.readlane.i32(i32 %a, i32 %lane)
  ret i32 %1
}

; CHECK-LABEL: define i32 @wave_active_countbits(
define i32 @wave_active_countbits(i1 %a) {
  ; CHECK: call i32 @llvm.dx.wave.active.countbits(i1 %a)
  %1 = call i32 @llvm.dx.wave.active.countbits(i1 %a)
  ret i32 %1
}

; CHECK-LABEL: define i32 @dot4add_i8packed(
define i32 @dot4add_i8packed(i32 %acc, i32 %a, i32 %b) {
  ; CHECK: call i32 @llvm.dx.dot4add.i8packed(i32 %acc, i32 %a, i32 %b)
  %1 = call i32 @llvm.dx.dot4add.i8packed(i32 %acc, i32 %a, i32 %b)
  ret i32 %1
}

; CHECK-LABEL: define i1 @is_finite_f32(
define i1 @is_finite_f32(float %a) {
  ; CHECK: call i1 @llvm.is.fpclass.f32(float %a, {{.*}}i32 504)
  %1 = call i1 @llvm.is.fpclass.f32(float %a, i32 504)
  ret i1 %1
}

; CHECK-LABEL: define i1 @is_normal_f32(
define i1 @is_normal_f32(float %a) {
  ; CHECK: call i1 @llvm.is.fpclass.f32(float %a, {{.*}}i32 264)
  %1 = call i1 @llvm.is.fpclass.f32(float %a, i32 264)
  ret i1 %1
}

declare float @llvm.sin.f32(float)
declare float @llvm.dx.frac.f32(float)
declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare i32 @llvm.ctpop.i32(i32)
declare float @llvm.maxnum.f32(float, float)
declare i32 @llvm.umin.i32(i32, i32)
declare float @llvm.fmuladd.f32(float, float, float)
declare i32 @llvm.dx.umad.i32(i32, i32, i32)
declare float @llvm.dx.dot2.f32(float, float, float, float)
declare double @llvm.dx.asdouble.i32(i32, i32)
declare i1 @llvm.dx.wave.all.equal.i32(i32)
declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
declare i32 @llvm.dx.wave.active.countbits(i1)
declare i32 @llvm.dx.dot4add.i8packed(i32, i32, i32)
declare i1 @llvm.is.fpclass.f32(float, i32)
