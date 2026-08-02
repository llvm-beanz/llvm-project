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

; An opcode this pass does not (yet) cover must be left as an unmodified
; `dx.op.*` call rather than erroring, since op raising is expected to grow
; incrementally (see feme/docs/Design.md).
; CHECK-LABEL: define float @unhandled_opcode(
define float @unhandled_opcode(float %a, float %b) {
  ; CHECK: call float @dx.op.binary.f32(i32 35, float %a, float %b)
  %1 = call float @dx.op.binary.f32(i32 35, float %a, float %b)
  ret float %1
}

declare float @dx.op.unary.f32(i32, float)
declare i1 @dx.op.isSpecialFloat.f32(i32, float)
declare i32 @dx.op.threadId.i32(i32, i32)
declare i32 @dx.op.flattenedThreadIdInGroup.i32(i32)
declare i1 @dx.op.waveIsFirstLane.i1(i32)
declare float @dx.op.binary.f32(i32, float, float)
