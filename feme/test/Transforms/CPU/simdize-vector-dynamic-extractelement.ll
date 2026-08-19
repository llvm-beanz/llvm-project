; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step C3 (feme/docs/Roadmap.md): a non-constant-index
; `extractelement` reading from a decomposed vector is no longer diagnosed
; ("a shuffle or a dynamic index becomes selects across the components",
; "Vectors become components, not nested vectors" in "Phase 4: Widening").
; `widenExtractElement` builds a `select` chain comparing the widened index
; against each component's compile-time position, since there is no single
; `<W x elemT>` vector a real per-lane-varying extract could read out of.

; CHECK-LABEL: define void @main(
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id
; CHECK: %[[IDX0:.*]] = icmp eq <4 x i32> %[[TID]], zeroinitializer
; CHECK: %[[SEL0:.*]] = select <4 x i1> %[[IDX0]], <4 x float> {{.*}}, <4 x float> poison
; CHECK: %[[IDX1:.*]] = icmp eq <4 x i32> %[[TID]], splat (i32 1)
; CHECK: %[[SEL1:.*]] = select <4 x i1> %[[IDX1]], <4 x float> splat (float 1.000000e+00), <4 x float> %[[SEL0]]
; CHECK: %[[IDX2:.*]] = icmp eq <4 x i32> %[[TID]], splat (i32 2)
; CHECK: %[[SEL2:.*]] = select <4 x i1> %[[IDX2]], <4 x float> splat (float 2.000000e+00), <4 x float> %[[SEL1]]
; CHECK: %[[IDX3:.*]] = icmp eq <4 x i32> %[[TID]], splat (i32 3)
; CHECK: select <4 x i1> %[[IDX3]], <4 x float> splat (float 3.000000e+00), <4 x float> %[[SEL2]]
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v = insertelement <4 x float> poison, float %tidf, i32 0
  %v2 = insertelement <4 x float> %v, float 1.000000e+00, i32 1
  %v3 = insertelement <4 x float> %v2, float 2.000000e+00, i32 2
  %v4 = insertelement <4 x float> %v3, float 3.000000e+00, i32 3
  %e = extractelement <4 x float> %v4, i32 %tid
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
