; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L466: like `loop-divergent-exit-non-phi-exit-relay-value.ll`,
; but the loop's own separate, genuinely uniform exit check is `latch`'s
; *own* terminator (a real, direct edge to `exit`) rather than some other,
; non-`latch` block's (`loop-divergent-exit-carried-value-feeds-uniform-
; check.ll`'s own `body`-block shape, which `OtherCondBrBlocks` handles
; completely separately and is unaffected by this fix). Reduced from
; Clang's own `Basic/Mandelbrot.test`, whose
; `for (; Iteration < MaxIteration; ++Iteration) { if (Diverged) break; }`
; loop has exactly this shape once `StructurizeCFG` relays both exits
; through the loop's own latch block: a per-pixel divergent `break` (the
; escape-radius check) and a separate, genuinely uniform hard iteration
; cap (`MaxIteration`), both reaching the same post-loop `if (Diverged)`
; merge directly from `latch`.
;
; Before this fix, `latch` already had its *own* pre-existing incoming
; value on `exit`'s merge phi (the literal `false`, wired up by
; `StructurizeCFG` under scalar semantics: reaching this edge at all
; already proves the thread never took `header`'s own divergent exit).
; The restore loop that re-attaches `header`'s own frozen relay value
; onto `latch`'s new edge only ever *added* a missing incoming value
; (guarded by `getBasicBlockIndex(Latch) == -1`), so it skipped this case
; entirely, silently discarding the frozen value and leaving every lane
; to read the stale, scalar-only constant unconditionally -- exactly the
; "any SIMD lane group containing one slow-to-escape lane marks every
; lane in that group 'not diverged'" bug Clang's own Mandelbrot sample
; exhibited.
;
; The fix blends the two together with a `select`, keyed on "has this
; lane diverged via `header`'s own check, as of now or any earlier
; round" (`active.header.live`'s own negation): a lane that is still
; genuinely active keeps `latch`'s own original value; one that already
; diverged earlier keeps reading its own frozen relay value instead.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %exit.relay.shadow = phi i1 [ poison, %entry ], [ %exit.relay.shadow.frozen, %latch ]
; CHECK: latch:
; CHECK: %[[NOT:.+]] = xor i1 %active.header.live, true
; CHECK-NEXT: %exited.via.header.this.iter = and i1 %active.live, %[[NOT]]
; CHECK-NEXT: %exit.relay.shadow.frozen = select i1 %exited.via.header.this.iter, i1 true, i1 %exit.relay.shadow
; CHECK-NEXT: %diverged.via.header.by.now = xor i1 %active.header.live, true
; CHECK-NEXT: %exit.relay.latch.blend = select i1 %diverged.via.header.by.now, i1 %exit.relay.shadow.frozen, i1 false
; CHECK: exit:
; CHECK-NEXT: %result = phi i1 [ %exit.relay.latch.blend, %latch ]
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %body
body:
  br label %latch
latch:
  %i.next = add i32 %i, 1
  %trip = icmp slt i32 %i.next, 10
  br i1 %trip, label %header, label %exit
exit:
  %result = phi i1 [ true, %header ], [ false, %latch ]
  store i1 %result, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
