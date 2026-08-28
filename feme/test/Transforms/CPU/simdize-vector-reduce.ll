; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6g-b-a-i-a-i-b: a `llvm.vector.reduce.and`/`or`/... call folding a
; divergent, per-lane-decomposed vector's components together is a supported
; consumer shape (`isSupportedVectorReduceIntrinsic`/`widenVectorReduce` in
; SIMDize.cpp) -- the shape glslang's `all`/`any`-style GLSL builtins take
; over a component-wise vector comparison, e.g.
; `llvm.vector.reduce.and.v4i1(fcmp ole <4 x float> %a, %b)`, confirmed by
; reducing a real failing `dEQP-VK.mesh_shader.ext.in_out.32_bits_only` case
; down to its exact IR shape. Unlike every other supported producer chain,
; the reduce's own result is not itself vector-typed: it folds back down to
; a single lane-wise `<W x i1>` scalar-shaped value (see `widenVectorReduce`'s
; own comment for why this lands in the ordinary `Widened` map instead of
; `WidenedVectorComponents`).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x i1>>
; CHECK-COUNT-4: fcmp ole <4 x float> {{.*}}, {{.*}}
; CHECK: and <4 x i1>
; CHECK: and <4 x i1>
; CHECK: and <4 x i1>
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %a0 = insertelement <4 x float> poison, float %tidf, i32 0
  %b0 = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  %cond = fcmp ole <4 x float> %a0, %b0
  %all = call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %cond)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.vector.reduce.and.v4i1(<4 x i1>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
