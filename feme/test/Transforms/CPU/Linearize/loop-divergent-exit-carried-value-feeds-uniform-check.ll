; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H125(a): like `loop-divergent-exit-carried-value.ll`, but `%i`
; (the loop-carried value read after the loop exits, via `exit`'s own
; `%i.result` merge) *also* feeds a second, genuinely uniform in-loop exit
; check (`%trip`, comparing `%i.next` against the compile-time constant
; `10`) that reaches the cycle's own single `exit` block directly, exactly
; the "real, uniform exit check ... coexist[ing] with a single, separate
; divergent exit check elsewhere" shape `linearizeCycle`'s own
; `OtherCondBrBlocks` handling is built to leave completely alone (see its
; own comment; `body`'s single `OtherCondBrBlocks` entry here is not
; widened into this cycle's own masking, since the loop's body has no
; mask-affecting op for roadmap L419(h)'s widening to apply to). Before
; this fix, `freezeLoopCarriedValues` froze `%i` itself in place (needed
; for its own `exit`-block use), which also retroactively made `%trip`'s
; condition divergent per `UniformityInfo` (a `phi` with any divergent
; incoming value is divergent, full stop), corrupting `body`'s own,
; otherwise untouched branch and tripping `feme-cpu-simdize`'s own "has a
; divergent branch" diagnostic (see `simdize-loop-divergent-exit-carried-
; value-feeds-uniform-check.ll`, which runs this exact shape through both
; passes). Reduced from Clang's own `Basic/Mandelbrot.test`
; (`feme-cpu-simdize: function 'main' has a divergent branch`), whose
; `for (; Iteration < MaxIteration; ++Iteration) { if (Diverged) break; }`
; loop has exactly this shape: `Iteration`, read after the loop, also
; feeds the loop's own compile-time-bounded trip-count check.
;
; `%i`'s own `header` phi must stay completely untouched -- still backed
; directly by `%i.next` -- so `body`'s own `%trip` branch, reading `%i`
; only via `%i.next`, keeps whatever uniformity it had before this cycle
; was linearized at all. A new, separate shadow `phi` instead carries the
; frozen value `exit`'s own merge actually needs.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK-NEXT: %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
; CHECK: %i.frozen.shadow = phi i32 [ 0, %entry ], [ %i.frozen.shadow.frozen, %latch ]
; CHECK: body:
; CHECK: %i.next = add i32 %i, 1
; CHECK-NEXT: %trip = icmp slt i32 %i.next, 10
; CHECK-NEXT: br i1 %trip, label %latch, label %exit
; CHECK: latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(
; CHECK: %i.frozen.shadow.frozen = select i1 %active.header.live, i32 %{{.*}}, i32 %i.frozen.shadow
; CHECK: exit:
; CHECK-NEXT: %i.result = phi i32 [ %i.next, %body ], [ %i.frozen.shadow, %latch ]
; CHECK-NEXT: store i32 %i.result, ptr %out
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %body
body:
  %i.next = add i32 %i, 1
  %trip = icmp slt i32 %i.next, 10
  br i1 %trip, label %latch, label %exit
latch:
  br label %header
exit:
  %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
  store i32 %i.result, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
