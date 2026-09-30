; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L274, second root cause found only via a real CTS re-run after
; the nested-GEP validation widening (simdize-groupshared-nested-struct-
; array.ll) alone did *not* clear any of the 64
; `dEQP-VK.glsl.atomic_operations.*_{compute,mesh,task}_{shared,payload}`
; cases it was meant to fix: `llvm::convertUsersOfConstantsToInstructions`'s
; per-`(Constant, BasicBlock)` memoization (see
; `coalesceIdenticalGroupSharedGEPs`'s own doc comment) can materialize
; more than one structurally-identical, but distinct, instance of the
; *same* constant-expression `getelementptr` within one block when a
; per-lane broadcast `insertelement` chain needs the same nested address
; re-materialized at each of several sibling insertion points out of
; program order -- and this duplication is not limited to a first-level
; GEP off the groupshared global; it applies identically to a *nested*
; one (a GEP whose own pointer operand is another GEP, ultimately rooted
; at the global). `matchPointerBroadcasts`'s per-lane walk requires every
; link of the chain to insert the exact same producer `Value` (identity,
; not just structural equality), so two `isIdenticalTo`-equal but
; distinct nested-GEP instances feeding different links of the same
; logical broadcast -- as this test builds by hand, `%g0`..`%g3`, one per
; lane -- break that recognition entirely, and the whole access is
; rejected as an unsupported user even though `isSupportedGroupShared
; NestedGEPUser` (L274 part 1) already accepts the nested chain shape
; itself. `coalesceIdenticalGroupSharedGEPs`'s own predicate previously
; only ever matched a *first-level* GEP's pointer operand against the
; global directly; `isGroupSharedRootedGEP` generalizes it to walk up an
; arbitrary chain of parent GEPs, so `%g0`..`%g3` (all structurally
; identical) get coalesced back down to one canonical instance before
; `matchPointerBroadcasts` ever has to reason about them.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %inner{{[0-9]*}} = getelementptr inbounds { i32, [4 x i32] }, ptr %shared.flat, i32 0, i32 1
; CHECK-NEXT: %g0{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %inner{{[0-9]*}}, i32 0, i32 2
; CHECK-NOT: getelementptr
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> splat (i32 1), <4 x ptr> {{.*}}, <4 x i1> splat (i1 true))
define void @main() #0 {
  %inner = getelementptr inbounds { i32, [4 x i32] }, ptr addrspace(3) @shared, i32 0, i32 1
  %g0 = getelementptr inbounds [4 x i32], ptr addrspace(3) %inner, i32 0, i32 2
  %g1 = getelementptr inbounds [4 x i32], ptr addrspace(3) %inner, i32 0, i32 2
  %g2 = getelementptr inbounds [4 x i32], ptr addrspace(3) %inner, i32 0, i32 2
  %g3 = getelementptr inbounds [4 x i32], ptr addrspace(3) %inner, i32 0, i32 2
  %v0 = insertelement <4 x ptr addrspace(3)> poison, ptr addrspace(3) %g0, i32 0
  %v1 = insertelement <4 x ptr addrspace(3)> %v0, ptr addrspace(3) %g1, i32 1
  %v2 = insertelement <4 x ptr addrspace(3)> %v1, ptr addrspace(3) %g2, i32 2
  %v3 = insertelement <4 x ptr addrspace(3)> %v2, ptr addrspace(3) %g3, i32 3
  call void @llvm.masked.scatter.v4i32.v4p3(<4 x i32> splat (i32 1), <4 x ptr addrspace(3)> %v3, i32 4, <4 x i1> splat (i1 true))
  ret void
}
@shared = internal addrspace(3) global { i32, [4 x i32] } undef
declare void @llvm.masked.scatter.v4i32.v4p3(<4 x i32>, <4 x ptr addrspace(3)>, i32 immarg, <4 x i1>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
