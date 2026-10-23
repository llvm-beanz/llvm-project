; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(l): the second of this audit's two `closeLatch`/
; `freezeLoopCarriedValues` call sites found to share `L521`'s own
; `BodyMask`-vs-`NaturalContinue` gap -- this one in the
; `DivergentCandidates`-non-empty ("a separate `CheckBlock` elsewhere in
; the loop body holds the real divergent exit check") handling, rather
; than `L521`'s own promoted-`Latch` general-fallback path.
;
; `%v` is a loop-carried induction variable, read again after the loop
; (`exit: store i32 %v, ...`) and also feeding the loop's own separate,
; genuinely uniform natural trip-count check at `latch`
; (`%loop.cond = icmp slt i32 %v.next, %n`) -- exactly the shape
; `feedsInLoopBranchCondition` detects to route `%v` through
; `freezeLoopCarriedValues`'s `FeedsUniformBranch` shadow-`phi` mechanism,
; gated on its own `BodyMask` parameter (`"will this lane get another
; round"`). `body`'s own divergent per-thread `break` check
; (`%tid == %v`) is this cycle's real exit check, matched as a
; `DivergentCandidates` member (`CheckBlock == body`), not `Header` or
; `Latch` directly -- reaching this file's *other* `closeLatch` call site
; than `L521`'s own.
;
; Before this fix, that call site's own `BodyMask` was fed only this
; check's own post-check liveness (`EffectiveMasksAfterCheck.Live`),
; never conjoined with `Latch`'s own separate, uniform natural condition
; -- exactly `L521`'s own bug, just at the other call site `L521` left
; out of scope. For the one lane (here, the thread whose own id never
; matches `%v`, so it never personally takes the divergent `break`) that
; instead stops via the group's shared uniform natural condition, the
; unconjoined mask stayed `true` through the final round, so the shadow
; kept selecting `%v.next` (this round's incoming, about-to-be-applied
; value) instead of `%v` (the value it actually had entering this,
; its last, round) -- reading one iteration ahead of the reference
; per-lane-independent semantics, which never actually loops back to
; `header` (and so never advances `%v` again) on a natural exit.
;
; Fixed the same way `L521` was: pass `closeLatch`'s own
; `OutPerLaneContinue` out-parameter (`%v`'s own natural condition,
; conjoined with this check's own post-check liveness) through to
; `freezeLoopCarriedValues` as `BodyMask`, instead of the raw liveness
; mask alone.

; CHECK-LABEL: define void @main(
; CHECK: latch:
; CHECK: %v.next = add i32 %v, 1
; CHECK: %loop.cond = icmp slt i32 %v.next, %n
; CHECK-DAG: [[NATURAL:%.*]] = and i1 {{.*}}, %loop.cond
; CHECK-DAG: %v.frozen = select i1 [[NATURAL]], i32 %v.next, i32 %v
; CHECK: exit:
; CHECK: %v.frozen.shadow.exit = phi i32 [ %v.frozen.shadow.frozen, %latch ]
; CHECK-NEXT: store i32 %v.frozen.shadow.exit, ptr %out
define void @main(i32 %n, ptr %out) #0 {
entry:
  br label %header
header:
  %v = phi i32 [0, %entry], [%v.next, %latch]
  br label %body
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %v
  br i1 %break.cond, label %exit, label %latch
latch:
  %v.next = add i32 %v, 1
  %loop.cond = icmp slt i32 %v.next, %n
  br i1 %loop.cond, label %header, label %exit
exit:
  store i32 %v, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
