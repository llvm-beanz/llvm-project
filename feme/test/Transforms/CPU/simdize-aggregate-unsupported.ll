; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent value of aggregate (struct) type is diagnosed rather than
; crashing: "Vectors become components, not nested vectors" in "Phase 4:
; Widening" describes decomposing a divergent aggregate the same way as a
; divergent vector (one widened value per scalar leaf), which is not yet
; implemented for aggregates -- only a divergent vector built by a
; constant-index `insertelement` chain is (see `simdize-vector-resource-
; store.ll`, and `checkVectorDecompositionSupported` in SIMDize.cpp).

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent value 'v' of aggregate type
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v = insertvalue { float, float } poison, float %tidf, 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
