; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(k): like `loop-divergent-exit-non-phi-exit-use.ll`
; (a value computed fresh in `header`, not itself a loop-carried `phi`,
; read outside the loop with no `phi` of its own at `exit`), but `body`
; -- a block strictly between `header`'s own divergent exit check and
; `latch`, inside `collectUniformPassThroughRegion`'s own uniform
; pass-through region -- *also* has its own direct, `latch`-bypassing
; edge to `exit` (exactly like `loop-divergent-exit-uniform-bypass-
; exit.ll`, but for a non-phi value's own LCSSA freeze instead of a
; `HeaderExitRelayValues`-captured constant).
;
; Before this fix, `freezeLoopCarriedValues`'s own `ShadowExitFixup`
; resolution built `%sq.lcssa.frozen` (correctly selecting between the
; fresh `%sq` and the prior round's own frozen value, gated on the
; *entering* mask `%active.live`) but only ever wired it onto `exit`'s
; `latch`-sourced incoming edge, reusing the raw, one-round-stale
; `%sq.lcssa` phi directly on `body`'s own edge instead -- on the
; assumption (correct for `HeaderExitRelayValues`'s own *constant*
; captures, but not here) that any non-`latch` predecessor of `exit`
; only ever carries a lane that took the loop's own separate, genuinely
; uniform exit, never its divergent one. `body`'s own `%bypass.cond`
; check here is itself genuinely uniform (so `collectUniformPassThrough
; Region` accepts this shape at all), but the *lane* reaching `exit` via
; it can still be one whose own divergent `%break.cond` check (in
; `header`) was true on *this exact round* -- exactly the shape that hung
; `dEQP-VK.graphicsfuzz.cov-nested-loops-assign-vector-elements-from-
; matrix-no-negative-indexing` forever, carrying a stale, one-round-old
; induction-variable value back into the loop's own re-test instead of
; the fresh value that actually satisfied it.
;
; `%sq.lcssa.frozen`'s own operands (`%active.live`, `%sq`, `%sq.lcssa`)
; are all `header`-resident, so it is valid -- and, per the comment
; above, necessary -- to reuse the identical value on `body`'s own edge
; too, rather than building a second, separate freeze there.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %sq.lcssa = phi i32 [ poison, %entry ], [ %sq.lcssa.frozen, %latch ]
; CHECK: %sq = mul i32 %i, %i
; CHECK: %sq.lcssa.frozen = select i1 %active.live, i32 %sq, i32 %sq.lcssa
; CHECK: body:
; CHECK: br i1 %bypass.cond, label %exit, label %cont
; CHECK: exit:
; CHECK-NEXT: %sq.lcssa.exit = phi i32 [ %sq.lcssa.frozen, %latch ], [ %sq.lcssa.frozen, %body ]
; CHECK-NEXT: store i32 %sq.lcssa.exit, ptr %out
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %sq = mul i32 %i, %i
  %break.cond = icmp sgt i32 %sq, %tid
  br i1 %break.cond, label %exit, label %body
body:
  %bypass.cond = icmp eq i32 %i, 3
  br i1 %bypass.cond, label %exit, label %cont
cont:
  br label %latch
latch:
  %i.next = add i32 %i, 1
  %trip = icmp slt i32 %i.next, 10
  br i1 %trip, label %header, label %exit
exit:
  store i32 %sq, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
