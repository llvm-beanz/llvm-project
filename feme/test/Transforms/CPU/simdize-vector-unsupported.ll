; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A divergent vector value is only decomposed as an `insertelement` chain, a
; `phi`, a `select` (with a scalar or per-lane vector condition, roadmap
; H6g-b-a-i-a-i-b), a `shufflevector`, a vector comparison (`fcmp`/`icmp`,
; roadmap H6g-b-a-i-a-i-b), a vector-typed resource load, or (roadmap L27) a
; vector-typed `extractvalue` reading a whole `FixedVectorType` leaf back
; out of a divergent aggregate; a use is only supported when it is another
; link of an `insertelement` chain, a matched resource-store call's
; stored-value operand, an `extractelement` (constant- or non-constant-
; index, see `simdize-vector-dynamic-extractelement.ll`), a `select`'s
; condition/true/false operand, a `shufflevector`'s vector operand, a
; `phi`'s incoming value, an `fcmp`/`icmp` operand, a
; `llvm.vector.reduce.*`/homogeneous-vectorizable-intrinsic (e.g.
; `llvm.minnum`/`llvm.maxnum`/`llvm.smin`/`llvm.smax`) call argument,
; another elementwise arithmetic/cast operand, an `insertvalue`'s own
; inserted-value operand (roadmap L27, e.g. a whole vector inserted as a
; single struct-field leaf, see `simdize-aggregate-vector-leaf.ll`), or an
; ordinary (non-groupshared) `store`'s own value operand (roadmap L27,
; e.g. that same vector-typed leaf stashed back through a local scratch
; array) (see `checkVectorDecompositionSupported` in SIMDize.cpp, and
; roadmap steps C3, H6g-b-a-i-a-i-b, and L27 in feme/docs/Roadmap.md for
; the shapes closed since milestone 7's own deviation note; see
; `simdize-vector-fcmp-select.ll` for the now-supported
; per-lane-condition-select shape). An arbitrary, unmatched call's own
; argument is still diagnosed: none of the shapes this pass needs
; decompose a vector into a plain function-call argument.
declare void @unrelated_callee(<4 x float>)

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent vector value 'va' used outside a supported insertelement-chain/insertvalue/ordinary-store/resource-store/extractelement/select/shufflevector/phi/elementwise/comparison/reduce/vectorizable-intrinsic pattern
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %va = insertelement <4 x float> poison, float %tidf, i32 0
  call void @unrelated_callee(<4 x float> %va)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
