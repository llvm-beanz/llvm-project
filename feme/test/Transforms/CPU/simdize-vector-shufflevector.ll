; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step C3 (feme/docs/Roadmap.md): a `shufflevector`'s mask is always
; a compile-time constant in LLVM IR ("a shuffle ... becomes selects across
; the components", "Vectors become components, not nested vectors" in
; "Phase 4: Widening"), so it decomposes with no runtime work at all: each
; output component is simply one of the two operands' already-widened
; components, picked at compile time (see
; `checkVectorDecompositionSupported`'s file comment and
; `FunctionWidener::widenShuffleVector` in SIMDize.cpp).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-NOT: shufflevector
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id
; CHECK: %[[TIDF:.*]] = sitofp <4 x i32> %[[TID]] to <4 x float>
; CHECK: fadd <4 x float> splat (float 1.000000e+00), %[[TIDF]]
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v0 = insertelement <4 x float> poison, float %tidf, i32 0
  %v1 = insertelement <4 x float> %v0, float 1.000000e+00, i32 1
  %v2 = insertelement <4 x float> %v1, float 2.000000e+00, i32 2
  %v3 = insertelement <4 x float> %v2, float 3.000000e+00, i32 3
  %swz = shufflevector <4 x float> %v3, <4 x float> poison, <4 x i32> <i32 1, i32 0, i32 2, i32 3>
  %e0 = extractelement <4 x float> %swz, i32 0
  %e1 = extractelement <4 x float> %swz, i32 1
  %sum = fadd float %e0, %e1
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
