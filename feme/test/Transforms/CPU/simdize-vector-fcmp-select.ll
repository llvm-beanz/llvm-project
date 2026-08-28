; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6g-b-a-i-a-i-b: a divergent vector comparison (`fcmp`/`icmp`) is a
; supported producer, decomposing into one per-component `<W x i1>`
; comparison exactly like ordinary elementwise arithmetic (see
; `checkVectorDecompositionSupported`'s file comment and
; `FunctionWidener::widenVectorElementwise` in SIMDize.cpp), and its
; `<N x i1>` result is, in turn, a supported *consumer* shape for a `select`'s
; now-per-lane vector condition (`widenVectorSelect`) -- the common
; component-wise `lessThanEqual`-style GLSL comparison feeding a per-lane
; `select`/`mix`, e.g. `dEQP-VK.mesh_shader.ext.in_out.32_bits_only`'s own
; `%8 = insertelement <4 x float> %6, float %7, i64 3` used by
; `%16 = fcmp ole <4 x float> %8, %15`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x i1>>
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: fcmp ole <4 x float> {{.*}}, {{.*}}
; CHECK-COUNT-4: select <4 x i1> {{.*}}, <4 x float> {{.*}}, <4 x float> {{.*}}
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %a0 = insertelement <4 x float> poison, float %tidf, i32 0
  %b0 = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  %t0 = insertelement <4 x float> poison, float 2.000000e+00, i32 0
  %cond = fcmp ole <4 x float> %a0, %b0
  %v = select <4 x i1> %cond, <4 x float> %t0, <4 x float> %a0
  %e0 = extractelement <4 x float> %v, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
