; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent value of vector type is diagnosed rather than crashing:
; "Vectors become components, not nested vectors" in "Phase 4: Widening"
; describes decomposing a divergent `<N x T>` value into `N` separate
; `<W x T>` components (LLVM has no `<W x <N x T>>`), which is not yet
; implemented (roadmap milestone 7 deviation).

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent value 'v' of vector or aggregate type
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v = insertelement <4 x float> poison, float %tidf, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
