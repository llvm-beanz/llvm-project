; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A divergent call to a "trivially vectorizable" math libcall -- both a
; target-independent one (`llvm.sqrt.f32`) and a homogeneous DXIL-specific
; one (`llvm.dx.frac.f32`, raised by `feme::dxil::OpRaisingPass`'s
; `DirectOps` table) -- widens directly to that intrinsic's vector-typed
; overload ("Call to a math libcall" in "Phase 4: Widening"), rather than
; being rejected or scalarized.

; CHECK-LABEL: define void @main(
; CHECK: [[TIDF:%.*]] = uitofp <4 x i32> {{.*}} to <4 x float>
; CHECK: call <4 x float> @llvm.sqrt.v4f32(<4 x float> [[TIDF]])
; CHECK: call <4 x float> @llvm.dx.frac.v4f32(<4 x float>
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = uitofp i32 %tid to float
  %s = call float @llvm.sqrt.f32(float %tidf)
  %f = call float @llvm.dx.frac.f32(float %s)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare float @llvm.sqrt.f32(float)
declare float @llvm.dx.frac.f32(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
