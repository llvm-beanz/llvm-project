; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV3(c)(10): `%sq`, computed fresh in `header` on every
; iteration (not itself a loop-carried `phi`), is read directly at
; `exit` with no `phi` of its own -- valid SSA because `header` (where
; `%sq` is defined) dominates every predecessor of `exit` (`header`
; itself and `latch`, `header`'s own sole successor-besides-`exit`).
; Reduced from Clang's own `Basic/Mandelbrot.test`: the post-loop
; palette computation re-reads `X * X + Y * Y`, CSE'd with the
; identical in-loop escape-check expression that also decides
; `Diverged`. Before this fix, `freezeLoopCarriedValues` only ever
; scanned `header`'s own phis, so a value like `%sq` -- read outside the
; loop only via plain dominance, never through a phi -- went completely
; unfrozen: every lane in a masked group except the one with the
; group's own longest-running iteration count would read that lane's
; own later, further-recomputed `%sq` instead of its own value as of its
; individual exit.
;
; `%sq` is computed *before* the mask update it feeds (`%active.header.live`
; depends on `%break.cond`, which depends on `%sq`), so unlike an
; ordinary post-check loop-carried update, its own freeze must gate on
; the *entering* mask (`%active.live`), not the exiting one
; (`%active.header.live`) -- gating on the exiting mask would discard
; exactly the round's own value that caused the exit, keeping the
; group's single longest-running lane's own stale value for everyone
; else instead (confirmed via direct runtime comparison against a
; `numpy.float32` reference of Clang's own `Basic/Mandelbrot.test`).

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %sq.lcssa = phi i32 [ poison, %entry ], [ %sq.lcssa.frozen, %latch ]
; CHECK: %sq = mul i32 %i, %i
; CHECK: latch:
; CHECK: %sq.lcssa.frozen = select i1 %active.live, i32 %sq, i32 %sq.lcssa
; CHECK: exit:
; CHECK: %sq.lcssa.exit = phi i32 [ %sq.lcssa.frozen, %latch ]
; CHECK-NEXT: store i32 %sq.lcssa.exit, ptr %out
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %sq = mul i32 %i, %i
  %break.cond = icmp sgt i32 %sq, %tid
  br i1 %break.cond, label %exit, label %latch
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
