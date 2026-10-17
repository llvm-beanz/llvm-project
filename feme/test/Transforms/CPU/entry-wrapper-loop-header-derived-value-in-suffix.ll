; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H165 (feme/docs/Roadmap.md): a "barriers inside a uniform loop"
; shape (roadmap milestone 9) whose header computes some OTHER (non-phi)
; value from its own induction phi -- `%doubled` below, standing in for a
; `feme::cpu::SIMDizePass`-inserted per-iteration splat/broadcast of an
; induction variable -- that is read only *after* the loop has exited,
; not by the loop body itself. `matchLoopShape`'s own
; `HeaderDerivedValues` collection previously only looked for such a
; value's uses inside the loop body's own per-wave region, so a
; header-derived value used solely by the post-loop suffix chain got no
; `loopvarN` parameter at all: the suffix region's own clone was left
; referencing the original (soon to be deleted) header instruction,
; crashing `buildWrapperForLoop` with a "Use still stuck around after Def
; was destroyed" assertion once `Shape.Header`'s dead copy was erased.
; `%doubled` must instead get the same trailing-parameter treatment as any
; genuine induction, threaded into the suffix region exactly like it
; already was for the loop body.
;
; L429 (feme/docs/Roadmap.md) subsequently taught `HeaderDerivedValues` to
; special-case values (like `%doubled` here) that transitively depend on a
; `PersistentInduction` header phi (`%sum`): since such a phi has no single
; scalar value in `LoopHeaderBB` (it is wave-persistent, reload-rewritten
; from `barrier_spill` instead), the dependent value can no longer be
; cloned once into the header and threaded through a `loopvarN` parameter.
; Instead it is recomputed fresh at its original use site
; (`cloneHeaderChainAt`) once `%sum`'s own reload is in scope, so
; `@main.suffix0` now only takes the loop's own `loopvar0` induction and
; recomputes `%doubled` itself from the reloaded `%sum`.

; The post-loop suffix region reloads `%sum` from `barrier_spill` and
; recomputes `%doubled` in place, rather than taking it as its own
; trailing `loopvarN` parameter or referencing the (by then erased) header
; instruction directly.
; CHECK-LABEL: define internal void @main.suffix0(
; CHECK-SAME: i32 %loopvar0, ptr %barrier_spill)
; CHECK: %[[RELOAD:.*]] = load i32, ptr %sum.reload{{.*}}
; CHECK: %[[DOUBLED:[0-9]+]] = mul i32 %[[RELOAD]], 2
; CHECK: store i32 %[[DOUBLED]], ptr @out

; The loop header's own clone of `%doubled` (never used, since nothing in
; the header itself or the loop body reads it) is left dead, now computed
; from `poison` because the `PersistentInduction` header phi it depended
; on has already been erased by the time the header's one-shot clone
; happens.
; CHECK-LABEL: define void @feme_cpu_entry_main(
; CHECK: loop.header:
; CHECK: %{{[0-9]+}} = mul i32 poison, 2

define void @main() #0 {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %latch ]
  %doubled = mul i32 %sum, 2
  %i.next = add i32 %i, 1
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %latch, label %after

latch:
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %sum.next = add i32 %sum, %val
  br label %header

after:
  store i32 %doubled, ptr @out
  ret void
}

@shared = internal addrspace(3) global [4 x i32] undef
@out = internal global i32 0

declare i32 @llvm.dx.thread.id.in.group(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
