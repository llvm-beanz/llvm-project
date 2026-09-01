; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6m: a homogeneous "trivially vectorizable" intrinsic call
; (`llvm.fabs.v3f32`, see `isElementwiseVectorizableIntrinsic`) whose operand
; is *uniform* (not divergent) must stay exactly as it is, exactly like any
; other uniform instruction -- "Uniform: leave it exactly as it is" in
; "Phase 4: Widening" -- rather than being unconditionally decomposed into
; per-component wide vectors the way a *divergent* one is (see
; `simdize-vector-intrinsic.ll`). Reduced from a real
; `dEQP`-adjacent HLSL `Feature/HLSLLib/abs.32.test` failure whose exact IR
; shape is `{abs(In.xyz), abs(In.w)}`: `FunctionWidener::widenInstruction`
; used to widen this call's vector-typed elementwise-vectorizable-intrinsic
; shape unconditionally, ahead of the general `isDivergentAtDef` gate every
; other producer/consumer shape respects, erasing and replacing a uniform
; call whose own `extractelement` users -- correctly gated on uniformity,
; and so left unchanged -- kept referencing the since-erased value,
; observed as a `poison` read (see `SIMDize.cpp`'s file comment and
; `agent_thoughts.md` for the full reduction).

; CHECK-LABEL: define void @main(
; CHECK-NOT: poison
; CHECK: [[FABS:%.*]] = call <3 x float> @llvm.fabs.v3f32(<3 x float> <float -1.000000e+00, float -2.000000e+00, float -3.000000e+00>)
; CHECK: extractelement <3 x float> [[FABS]], i32 0
; CHECK: extractelement <3 x float> [[FABS]], i32 1
; CHECK: extractelement <3 x float> [[FABS]], i32 2
define void @main() #0 {
  %fabs = call <3 x float> @llvm.fabs.v3f32(
      <3 x float> <float -1.000000e+00, float -2.000000e+00, float -3.000000e+00>)
  %e0 = extractelement <3 x float> %fabs, i32 0
  %e1 = extractelement <3 x float> %fabs, i32 1
  %e2 = extractelement <3 x float> %fabs, i32 2
  %v0 = insertelement <3 x float> poison, float %e0, i32 0
  %v1 = insertelement <3 x float> %v0, float %e1, i32 1
  %v2 = insertelement <3 x float> %v1, float %e2, i32 2
  ret void
}
declare <3 x float> @llvm.fabs.v3f32(<3 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
