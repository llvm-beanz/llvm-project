; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step C3 (feme/docs/Roadmap.md): a divergent `phi` of vector type
; -- the shape `feme::cpu::LinearizePass` leaves at a uniform diamond's
; merge block reconciling two divergent vector values from its arms -- is a
; supported producer, decomposing into one per-component `phi` instead of
; being diagnosed (see `checkVectorDecompositionSupported`'s file comment,
; and `FunctionWidener::createWidenedVectorPHIStub`/
; `fillWidenedVectorPHIIncoming` in SIMDize.cpp).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK: end:
; CHECK-COUNT-4: phi <4 x float>
define void @main(i1 %cond) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  br i1 %cond, label %a, label %b
a:
  %va = insertelement <4 x float> poison, float %tidf, i32 0
  br label %end
b:
  %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  br label %end
end:
  %v = phi <4 x float> [ %va, %a ], [ %vb, %b ]
  %e0 = extractelement <4 x float> %v, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
