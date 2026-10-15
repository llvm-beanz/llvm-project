; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap K6y (CTS `dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrdiff.
; wg_comparisons_diff`, found only after K6x let `feme-cpu-linearize`
; succeed on this shape for the first time): a nested groupshared
; `getelementptr` whose own index is uniform, but whose *base* is itself
; a genuinely divergent `getelementptr` (`%row`, indexed by
; `llvm.dx.thread.id.in.group`), is still "divergent at def" overall --
; `UniformityInfo` propagates divergence from a divergent operand to
; every user, regardless of which operand supplied it -- so `%leaf`
; still dispatches to `FunctionWidener::widenGroupSharedGEP`, exactly
; like a directly-divergent-index GEP does.
;
; `widenGroupSharedGEP` had two bugs here, both fixed together:
; (1) it only ever substituted the widened form of a divergent *index*
; (`Widened.count(Idx) ? Widened[Idx] : Idx`), never of the *base*
; pointer operand -- so `%leaf`'s own, already-widened `%row` was
; silently dropped in favor of the stale, unwidened `%row` from the
; original function, an illegal cross-function `Value*` reference once
; the original function's own instructions were erased.
; (2) because `%leaf`'s own index (`%u`) stays uniform, fixing (1) alone
; could still (for a *different* index shape than this test's) leave the
; resulting `getelementptr` scalar-typed even though the dispatch gate
; judged `%leaf` divergent, violating `getWidened`'s all-vector-typed
; cache invariant for any later consumer -- observed via a crash in
; `widenGroupSharedStore`'s own `getWidened(SI.getPointerOperand())`
; call below (needed because the store's *value* operand, `%tid`, is
; genuinely divergent even where its address is not), reading back a
; bare scalar pointer where `CreateMaskedScatter` required a real
; `<4 x ptr addrspace(3)>`.
;
; This test's own shape happens to exercise only fix (1): substituting
; the already-vector `%row.wide` base makes `%leaf`'s own `getelementptr`
; naturally vector-typed too (a vector base implicitly broadcasts a
; scalar index to match, the same way a vector index already broadcasts
; a scalar base), with no explicit broadcast/splat needed at all -- fix
; (2) guards the narrower case where every operand, base included, stays
; scalar yet the gate still judged the result divergent (e.g. the
; `ptrtoint`-of-a-uniform-index-leaf shape `wg_comparisons_diff` itself
; hits, downstream of this very GEP).

; CHECK-LABEL: define void @main(
; CHECK: %leaf.wide{{[0-9]*}} = getelementptr [4 x i32], <4 x ptr> %row.wide{{[0-9]*}}, i32 0, i32 %u
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> {{.*}}, <4 x ptr> {{.*}}%leaf.wide{{[0-9]*}}, <4 x i1> %wave_sideeffect_mask)
define void @main(i32 %u) #0 {
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %row = getelementptr [4 x [4 x i32]], ptr addrspace(3) @shared, i32 0, i32 %tid
  %leaf = getelementptr [4 x i32], ptr addrspace(3) %row, i32 0, i32 %u
  store i32 %tid, ptr addrspace(3) %leaf
  ret void
}
@shared = internal addrspace(3) global [4 x [4 x i32]] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
