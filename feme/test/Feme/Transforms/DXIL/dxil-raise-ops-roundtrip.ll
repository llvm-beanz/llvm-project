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

declare float @llvm.sin.f32(float)
declare float @llvm.dx.frac.f32(float)
declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
