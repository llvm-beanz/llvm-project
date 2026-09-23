; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L151: a `feme.cpu.resource.*` call sitting *after* a divergent
; `if`'s own reconvergence point, where the `if`'s true arm contains a
; loop with a divergent exit -- a reduction of
; `dEQP-VK.subgroups.ballot_broadcast.compute.subgroupbroadcastfirst_*`'s
; own shape (`subgroupBallot` + a loop-with-break finding the group's
; first active lane, then a divergent `if` for every non-first lane
; containing a *second*, structurally-identical loop, before an
; unconditional store every lane must reach).
;
; Root cause: `DiamondFlattener::run` treats the loop's own exit block
; (`loopexit` here) as a brand-new root in its own right (see
; diamond-after-loop.ll), always walking it all the way to a `ret` (before
; this fix). When that loop is nested inside an *enclosing* diamond's arm,
; that walk incorrectly re-visits -- and re-masks -- `merge` and
; everything after it a *second* time, using the loop-exit root's own
; narrower, stale entry mask (`sideeffect.t`, true only for lanes that
; took the `if`'s true arm) instead of leaving `merge` untouched: it was
; already correctly masked once by the *enclosing* diamond's own walk,
; using the properly-merged mask that (here, since neither arm narrows it
; any further) folds down to a plain `true` for every lane.

; CHECK-LABEL: define void @main(
; CHECK: merge:
; CHECK-NEXT: %live.merge = select i1 %c1, i1 %live.t, i1 %live.f
; CHECK-NEXT: %sideeffect.merge = select i1 %c1, i1 %sideeffect.t, i1 %sideeffect.f
; CHECK-NEXT: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 %sideeffect.merge)
define void @main(ptr %heap, i32 %heap_count) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp ne i32 %tid, 0
  br i1 %c1, label %if.true, label %if.false
if.false:
  br label %merge
if.true:
  br label %header
header:
  %i = phi i32 [0, %if.true], [%inc, %latch]
  %cond = icmp eq i32 %i, %tid
  br i1 %cond, label %loopexit, label %latch
latch:
  %inc = add i32 %i, 1
  br label %header
loopexit:
  br label %merge
merge:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
