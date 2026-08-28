; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6g-b-a-i-a-i-b: a homogeneous "trivially vectorizable" intrinsic
; call (`llvm.minnum`/`llvm.maxnum`/`llvm.smin`/`llvm.smax`/...) over an
; already-decomposed divergent vector operand is a supported producer *and*
; consumer shape -- the shape a GLSL `min`/`max`/`clamp` builtin over a
; vec-typed value takes -- decomposing into one per-component scalar-element
; intrinsic call exactly like ordinary elementwise arithmetic (see
; `checkVectorDecompositionSupported`'s file comment and
; `FunctionWidener::widenVectorElementwise` in SIMDize.cpp). This is a
; distinct root cause from this row's own `fcmp`/`icmp`/`select` fix: it is
; what actually dominates the row's own cited
; `dEQP-VK.mesh_shader.ext.in_out.*` bucket once the `fcmp`/`icmp`/`select`/
; `llvm.vector.reduce.*` fixes let those cases progress far enough to reach
; it (confirmed via the same one-off diagnostic-dump-and-single-case-rerun
; technique used to isolate this row's own initial shape; see
; `agent_thoughts.md` and `VulkanCTSReport.md` for the real CTS numbers).

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: call <4 x float> @llvm.maxnum.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %va = insertelement <4 x float> poison, float %tidf, i32 0
  %vb = insertelement <4 x float> poison, float 1.000000e+00, i32 0
  %clamped = call <4 x float> @llvm.maxnum.v4f32(<4 x float> %va, <4 x float> %vb)
  %e0 = extractelement <4 x float> %clamped, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <4 x float> @llvm.maxnum.v4f32(<4 x float>, <4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
