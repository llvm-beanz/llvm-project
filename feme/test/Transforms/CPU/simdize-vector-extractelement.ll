; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A constant-index `extractelement` reading from a decomposed vector -- an
; `insertelement` chain here (see simdize-vector-resource-store.ll for the
; resource-load producer shape) -- is a supported consumer (see
; `checkVectorDecompositionSupported`/`widenExtractElement` in SIMDize.cpp):
; it reads the already-widened `<W x elemT>` component straight back out of
; `WidenedVectorComponents` instead of needing a real per-lane extract out
; of a (nonexistent, since LLVM has no `<W x <N x T>>`) single wide vector.

; CHECK-LABEL: define void @main(
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id
; CHECK: %[[TIDF:.*]] = sitofp <4 x i32> %[[TID]] to <4 x float>
; CHECK: fadd <4 x float> %[[TIDF]], splat (float 2.000000e+00)
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %v = insertelement <4 x float> poison, float %tidf, i32 0
  %v2 = insertelement <4 x float> %v, float 1.0, i32 1
  %v3 = insertelement <4 x float> %v2, float 2.0, i32 2
  %v4 = insertelement <4 x float> %v3, float 3.0, i32 3
  %e0 = extractelement <4 x float> %v4, i32 0
  %e2 = extractelement <4 x float> %v4, i32 2
  %sum = fadd float %e0, %e2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
