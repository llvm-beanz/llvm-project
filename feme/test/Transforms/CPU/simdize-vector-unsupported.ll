; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent vector value is only decomposed when it is a constant-index
; `insertelement` chain or a vector-typed resource load, consumed solely by
; another link of that chain, a matched resource-store call's stored-value
; operand, or a constant-index `extractelement` (see
; `simdize-vector-resource-store.ll`/`simdize-vector-extractelement.ll` for
; those supported shapes, and `checkVectorDecompositionSupported` in
; SIMDize.cpp) -- the narrow slice of "Vectors become components, not
; nested vectors" ("Phase 4: Widening") this pass implements. A
; non-constant-index `extractelement` -- a genuinely per-lane-varying
; gather out of the vector -- is still diagnosed rather than crashing.

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent vector value 'v' used outside a supported insertelement-chain/resource-store/extractelement pattern
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v = insertelement <4 x float> poison, float %tidf, i32 0
  %e = extractelement <4 x float> %v, i32 %tid
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
