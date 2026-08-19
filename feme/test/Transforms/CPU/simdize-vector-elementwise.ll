; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step C3 (feme/docs/Roadmap.md): ordinary elementwise arithmetic
; over two divergent vectors -- the "color = a + b" shape every shader is
; full of -- decomposes into one scalar-element op per component instead of
; being diagnosed. "Vectors become components, not nested vectors" ("Phase
; 4: Widening") applies to a `BinaryOperator`/`UnaryOperator`/`CastInst` of
; vector type exactly like it does to a `phi`/`select`/`shufflevector` (see
; `checkVectorDecompositionSupported`'s file comment and
; `FunctionWidener::widenVectorElementwise` in SIMDize.cpp).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: fadd <4 x float>
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %va0 = insertelement <4 x float> poison, float %tidf, i32 0
  %va1 = insertelement <4 x float> %va0, float 1.000000e+00, i32 1
  %va2 = insertelement <4 x float> %va1, float 2.000000e+00, i32 2
  %va3 = insertelement <4 x float> %va2, float 3.000000e+00, i32 3
  %vb0 = insertelement <4 x float> poison, float 4.000000e+00, i32 0
  %vb1 = insertelement <4 x float> %vb0, float 5.000000e+00, i32 1
  %vb2 = insertelement <4 x float> %vb1, float 6.000000e+00, i32 2
  %vb3 = insertelement <4 x float> %vb2, float 7.000000e+00, i32 3
  %sum = fadd <4 x float> %va3, %vb3
  %e0 = extractelement <4 x float> %sum, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
