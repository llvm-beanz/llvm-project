; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent vector value is only decomposed as an `insertelement` chain, a
; `phi`, a scalar-condition `select`, a `shufflevector`, or a vector-typed
; resource load; a use is only supported when it is another link of an
; `insertelement` chain, a matched resource-store call's stored-value
; operand, an `extractelement` (constant- or non-constant-index, see
; `simdize-vector-dynamic-extractelement.ll`), a `select`'s true/false
; operand, a `shufflevector`'s vector operand, or a `phi`'s incoming value
; (see `checkVectorDecompositionSupported` in SIMDize.cpp, and roadmap step
; C3 in feme/docs/Roadmap.md for the shapes closed since milestone 7's own
; deviation note). A `select` with a per-lane `<N x i1>` condition is still
; diagnosed: none of the shapes this pass needs produce one, and decomposing
; it would need a per-component condition too, not just a per-component
; value.

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent vector value 'va' used outside a supported insertelement-chain/resource-store/extractelement/select/shufflevector/phi/elementwise pattern
define void @main(<4 x i1> %cond) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %va = insertelement <4 x float> poison, float %tidf, i32 0
  %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  %v = select <4 x i1> %cond, <4 x float> %va, <4 x float> %vb
  %e0 = extractelement <4 x float> %v, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
