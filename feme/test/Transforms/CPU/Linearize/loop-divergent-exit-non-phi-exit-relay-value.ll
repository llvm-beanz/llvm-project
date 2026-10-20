; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L420: like
; `loop-divergent-exit-carried-value-feeds-uniform-check.ll`, but the value
; read after the loop (`%diverged.result`) is *not* a reference to one of
; `header`'s own loop-carried phis -- it is a second `exit`-block merge
; whose two incoming values are themselves simple, fixed per-edge
; constants (`true` for "exited via the divergent `break`", `false` for
; "exited via the loop's own separate, genuinely uniform trip-count
; check"). Reduced from Clang's own `Basic/Mandelbrot.test`, whose
; `for (; Iteration < MaxIteration; ++Iteration) { if (Diverged) break; }`
; loop's post-loop `if (Diverged)` color selection reads exactly this
; shape of merge.
;
; Before this fix, this merge's `header`-path value (the literal `true`)
; was carried onto `latch`'s new edge completely unfrozen: `V` is a
; `ConstantInt`, not one of `header`'s own phis, so
; `freezeLoopCarriedValues`'s own use-rewriting scan -- which only ever
; redirects a *use of one of `header`'s own phis* -- never even sees it.
; Every lane that took the divergent exit on an earlier round than some
; other, still-active lane ended up reading whichever constant the
; group's *final* round happened to carry instead of its own, confirmed
; to eventually SIMDize into a group-wide broadcast scalar instead of a
; genuine per-lane vector (see the companion end-to-end
; `simdize-loop-divergent-exit-non-phi-exit-relay-value.ll`).
;
; A brand-new shadow `phi`, seeded `poison` on entry (never observably
; read by a lane that has not yet taken the divergent exit), carries the
; correctly-frozen per-lane value instead, updated via a `select` keyed
; on a new per-iteration "did this lane just exit via header's own
; check" mask.
;
; Roadmap L466: `body`'s own direct exit edge to `exit` (taken when the
; loop's separate, genuinely-uniform trip-count check fires) is itself a
; second, independent bypass of `latch` -- `body` is a `BodyRegion`
; member, not `latch` itself. The literal `false` on that edge is
; broadcast to *every* lane simultaneously when the uniform check fires,
; which silently clobbers any lane that already recorded `true` via an
; earlier round's divergent `header` exit (the exact same class of bug
; as the `latch`-relayed value above, just on a different incoming
; edge). It must likewise be blended against the per-lane shadow instead
; of applied unconditionally.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %exit.relay.shadow = phi i1 [ poison, %entry ], [ %exit.relay.shadow.frozen, %latch ]
; CHECK: body:
; CHECK: %exit.relay.bypass.blend = select i1 %{{.*}}, i1 %exit.relay.shadow.frozen.bypass, i1 false
; CHECK: latch:
; CHECK: %[[NOT:.+]] = xor i1 %active.header.live, true
; CHECK-NEXT: %exited.via.header.this.iter = and i1 %active.live, %[[NOT]]
; CHECK-NEXT: %exit.relay.shadow.frozen = select i1 %exited.via.header.this.iter, i1 true, i1 %exit.relay.shadow
; CHECK: exit:
; CHECK-NEXT: %i.result = phi i32 [ %i.next, %body ], [ %i.frozen.shadow, %latch ]
; CHECK-NEXT: %diverged.result = phi i1 [ %exit.relay.bypass.blend, %body ], [ %exit.relay.shadow.frozen, %latch ]
define void @main(ptr %out, ptr %out2) #0 {
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
  %diverged.result = phi i1 [ true, %header ], [ false, %body ]
  store i32 %i.result, ptr %out
  %diverged.ext = zext i1 %diverged.result to i32
  store i32 %diverged.ext, ptr %out2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
