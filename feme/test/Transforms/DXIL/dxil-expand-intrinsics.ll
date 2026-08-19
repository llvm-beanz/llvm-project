; RUN: feme-opt --llvm -passes=feme-dxil-expand-intrinsics -S %s | FileCheck %s

; feme::dxil::IntrinsicExpansionPass expands the `llvm.dx.*` math intrinsics
; feme::dxil::OpRaisingPass raises DXIL ops to -- which only LLVM's DirectX
; backend knows how to select -- into plain, target-agnostic LLVM IR, so the
; module can be re-targeted anywhere. See the "Raised LLVM IR" section of
; feme/docs/Design.md.

; CHECK-LABEL: define float @frac_f32(
define float @frac_f32(float %a) {
  ; CHECK: [[FLOOR:%.*]] = call float @llvm.floor.f32(float %a)
  ; CHECK: fsub float %a, [[FLOOR]]
  %1 = call float @llvm.dx.frac.f32(float %a)
  ret float %1
}

; CHECK-LABEL: define float @saturate_f32(
define float @saturate_f32(float %a) {
  ; CHECK: [[LOW:%.*]] = call float @llvm.maxnum.f32(float %a, float 0.000000e+00)
  ; CHECK: call float @llvm.minnum.f32(float [[LOW]], float 1.000000e+00)
  %1 = call float @llvm.dx.saturate.f32(float %a)
  ret float %1
}

; CHECK-LABEL: define float @rsqrt_f32(
define float @rsqrt_f32(float %a) {
  ; CHECK: [[SQRT:%.*]] = call float @llvm.sqrt.f32(float %a)
  ; CHECK: fdiv float 1.000000e+00, [[SQRT]]
  %1 = call float @llvm.dx.rsqrt.f32(float %a)
  ret float %1
}

; CHECK-LABEL: define i32 @imad_i32(
define i32 @imad_i32(i32 %a, i32 %b, i32 %c) {
  ; CHECK: [[MUL:%.*]] = mul i32 %a, %b
  ; CHECK: add i32 [[MUL]], %c
  %1 = call i32 @llvm.dx.imad.i32(i32 %a, i32 %b, i32 %c)
  ret i32 %1
}

; CHECK-LABEL: define float @dot3_f32(
define float @dot3_f32(float %a0, float %a1, float %a2, float %b0, float %b1, float %b2) {
  ; CHECK: [[M:%.*]] = fmul float %a0, %b0
  ; CHECK: [[F1:%.*]] = call float @llvm.fmuladd.f32(float %a1, float %b1, float [[M]])
  ; CHECK: call float @llvm.fmuladd.f32(float %a2, float %b2, float [[F1]])
  %1 = call float @llvm.dx.dot3.f32(float %a0, float %a1, float %a2, float %b0, float %b1, float %b2)
  ret float %1
}

; `llvm.dx.fdot` (SM6.9's unified, vector-operand `Dot2`..`Dot4` replacement,
; see the `FDot` comment in OpRaising.cpp) expands the same way, extracting
; each lane instead of reading it from a separate scalar operand.
; CHECK-LABEL: define half @fdot_v3f16(
define half @fdot_v3f16(<3 x half> %a, <3 x half> %b) {
  ; CHECK: [[A0:%.*]] = extractelement <3 x half> %a, i64 0
  ; CHECK: [[B0:%.*]] = extractelement <3 x half> %b, i64 0
  ; CHECK: [[M:%.*]] = fmul half [[A0]], [[B0]]
  ; CHECK: [[A1:%.*]] = extractelement <3 x half> %a, i64 1
  ; CHECK: [[B1:%.*]] = extractelement <3 x half> %b, i64 1
  ; CHECK: [[F1:%.*]] = call half @llvm.fmuladd.f16(half [[A1]], half [[B1]], half [[M]])
  ; CHECK: [[A2:%.*]] = extractelement <3 x half> %a, i64 2
  ; CHECK: [[B2:%.*]] = extractelement <3 x half> %b, i64 2
  ; CHECK: call half @llvm.fmuladd.f16(half [[A2]], half [[B2]], half [[F1]])
  %1 = call half @llvm.dx.fdot.v3f16(<3 x half> %a, <3 x half> %b)
  ret half %1
}

; CHECK-LABEL: define i1 @isinf_f32(
define i1 @isinf_f32(float %a) {
  ; CHECK: call i1 @llvm.is.fpclass.f32(float %a, {{.*}}i32 516)
  %1 = call i1 @llvm.dx.isinf.f32(float %a)
  ret i1 %1
}

; Intrinsics without a context-free definition in terms of standard LLVM
; operations (here a wave op) are left unmodified.
; CHECK-LABEL: define i32 @wave_lane_index(
define i32 @wave_lane_index() {
  ; CHECK: call i32 @llvm.dx.wave.getlaneindex()
  %1 = call i32 @llvm.dx.wave.getlaneindex()
  ret i32 %1
}

declare float @llvm.dx.frac.f32(float)
declare float @llvm.dx.saturate.f32(float)
declare float @llvm.dx.rsqrt.f32(float)
declare i32 @llvm.dx.imad.i32(i32, i32, i32)
declare float @llvm.dx.dot3.f32(float, float, float, float, float, float)
declare half @llvm.dx.fdot.v3f16(<3 x half>, <3 x half>)
declare i1 @llvm.dx.isinf.f32(float)
declare i32 @llvm.dx.wave.getlaneindex()
