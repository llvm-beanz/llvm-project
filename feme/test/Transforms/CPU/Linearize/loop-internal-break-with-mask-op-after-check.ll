; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L442: the exact complement of `loop-internal-break-with-mask-op.
; ll`'s own shape -- a resource call physically positioned *after* the
; widened `CheckBlock`'s own break condition in program order, rather
; than before it. That sibling test's own comment already documents (and
; deliberately preserves) the "before the check" case staying at its
; original, un-narrowed mask, since the check-narrowed mask's own
; defining instructions do not yet dominate a use that early. This test
; covers the opposite, previously-broken case: a call positioned at or
; after the check's own condition instruction *is* part of the iteration
; that check is deciding the validity of (exactly like a bounded,
; bounded-trip-count inner loop's own out-of-range iteration -- see the
; real, reproduced `dEQP-VK.compute.pipeline.
; zero_initialize_workgroup_memory.max_workgroup_memory` miscount this
; fixes), so it must be narrowed with the check-inclusive mask, not the
; stale mask the lane merely had while still live *entering* the check.
;
; Before this fix, `LoopLinearizer::linearizeCycle`'s `CheckBlock`
; handling called `applyStageMasks` over the whole block with the stale,
; pre-check `Masks` (computing the check-narrowed `MasksAfterCheck` only
; afterward), so this call below stuck at a mask that never reflected
; the check's own condition -- one wasted, out-of-range iteration's own
; side effect fired per wave, double-counting a lane. Fixed by splitting
; `applyStageMasks` into two sub-ranges at the check condition's own
; instruction, computing `MasksAfterCheck` right there instead of at the
; block's terminator, and using it to narrow only the sub-range at or
; after that point.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.check.live, %latch ]
; CHECK: %active.sideeffect = phi i1 [ true, %entry ], [ %active.check.sideeffect, %latch ]
; CHECK: body:
; CHECK: %break.cond = icmp sgt i32 %inc, %k
; CHECK: %active.check.live = and i1 %active.live,
; CHECK: %active.check.sideeffect = and i1 %active.sideeffect,
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 %active.check.sideeffect)
; CHECK: br label %latch
; CHECK: latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK: br i1 %loop.any.active, label %header, label %exit
define void @main(ptr %heap, i32 %heap_count, i32 %n, i32 %k) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %body, label %exit
body:
  %break.cond = icmp sgt i32 %inc, %k
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  br i1 %break.cond, label %exit, label %latch
latch:
  br label %header
exit:
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
