; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step C3 (feme/docs/Roadmap.md): a divergent `select` of vector
; type with a scalar `i1` condition is a supported producer, decomposing
; into one `select` per component sharing that single widened condition
; (see `checkVectorDecompositionSupported`'s file comment and
; `FunctionWidener::widenVectorSelect` in SIMDize.cpp). A `select` with a
; per-lane `<N x i1>` condition -- decomposed into one `select` per
; component instead, each with its own widened condition component -- is
; now also supported (roadmap H6g-b-a-i-a-i-b, see
; simdize-vector-fcmp-select.ll).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: select <4 x i1> %{{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}
define void @main(i1 %cond) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %va = insertelement <4 x float> poison, float %tidf, i32 0
  %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  %v = select i1 %cond, <4 x float> %va, <4 x float> %vb
  %e0 = extractelement <4 x float> %v, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
