; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(a): reduced from `dEQP-VK.graphicsfuzz.
; cov-basic-block-discard-in-function`'s own `do { a = func(b); b++; }
; while (b < 4); if (a == 3) ...` -- a plain uniform loop whose own induction
; variable (here `%b`, standing in for that test's own loop-carried `b`) is
; read again after the loop ends. `linearizeCycle`'s "general fallback"
; masking path (reached for any cycle whose own `Latch` sits behind a
; trivial, phi-less relay stub promoted from a real predecessor -- see
; `LatchWasPromoted`/roadmap `K6v` -- here, `%crit_edge`/`%check`) always
; calls `freezeLoopCarriedValues` to build a `%b`-equivalent shadow `phi`
; for this outside-the-loop read, gated on a `BodyMask` meant to answer
; "will this lane get another round" -- but that `BodyMask` used to be
; `Latch`'s own raw mask-affecting-op liveness (`MasksAtLatch.Live`)
; *alone*, never conjoined with `Latch`'s own separate, genuinely uniform
; natural trip-count condition (`closeLatch`'s own `NaturalContinue`). A
; loop like this one, whose liveness mask never actually narrows (no
; `discard`/`demote` ever fires), has `MasksAtLatch.Live` permanently
; `true` -- so the shadow kept selecting *next*-round's incoming value
; even on the very last round, the one whose own uniform trip check (not
; any liveness narrowing) is what actually ends the loop, landing one
; iteration ahead of where every after-the-loop reader expects (confirmed:
; `%b`'s own after-the-loop read came back as the loop's fully-exhausted
; trip count instead of its last `b`-before-increment value, permanently
; failing this test's own `== 3` comparison and rendering solid black
; instead of red). Fixed by conjoining `Latch`'s own natural condition
; into the mask `freezeLoopCarriedValues` freezes against (`closeLatch`'s
; new `OutPerLaneContinue` out-parameter).
;
; CHECK-LABEL: define void @main(
; CHECK: check:
; CHECK-DAG: [[NATURAL:%.*]] = and i1 {{.*}}, {{.*}}
; CHECK-DAG: %b.frozen = select i1 [[NATURAL]], i32 %b.next, i32 %b
; CHECK: exit:
; CHECK: %b.frozen.shadow.exit = phi i32 [ %b.frozen.shadow.frozen, %check ]
; CHECK-NEXT: icmp eq i32 %b.frozen.shadow.exit, 3
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %b = phi i32 [0, %entry], [%b.next, %crit_edge]
  br label %check
check:
  %b.next = add i32 %b, 1
  %done = icmp sge i32 %b.next, 4
  br i1 %done, label %exit, label %crit_edge
crit_edge:
  br label %header
exit:
  %a.eq3 = icmp eq i32 %b, 3
  %color = select i1 %a.eq3, <4 x float> <float 1.0, float 0.0, float 0.0, float 1.0>, <4 x float> zeroinitializer
  store <4 x float> %color, ptr %out
  ret void
}
attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
