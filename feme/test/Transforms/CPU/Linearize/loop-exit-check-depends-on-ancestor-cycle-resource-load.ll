; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -S %s -o /dev/null

; Roadmap ENV6(c)(2)(r): `inner`'s own exit check (`%inner.cond`) reads
; `%bound`, a `feme.cpu.resource.load.raw.i32` call sitting in `outer`'s
; own body (`outer.merge`), not `inner`'s own body at all.
; `LoopLinearizer::run` processes cycles in post-order -- `inner`, as
; `outer`'s child, is fully attempted *before* `outer` is -- so at the
; point `inner` is linearized, `%bound`'s own mask operand is still the
; literal `i1 true` `SPIRVResourceLoweringPass` gives it, unnarrowed by
; `outer`'s own masking (which only runs afterward, once `outer` itself is
; attempted). `UniformityInfo`'s one-time, pre-masking snapshot therefore
; (correctly, at the time) classifies `%inner.cond` uniform: nothing yet
; makes `%bound`'s result data-dependent on anything divergent.
;
; `outer`'s own masking, once it runs, narrows `%bound`'s mask operand in
; place (`narrowCallMask` mutates the call's existing mask argument, never
; replacing the call) to `%live.merge` -- a genuinely per-lane value,
; since `outer`'s body contains a `feme.stage.discard` guarded by a
; divergent (`atomicrmw`-derived) condition. `%bound`'s result, and so
; `%inner.cond` itself, is retroactively divergent by the time
; `feme-cpu-simdize`'s own fresh `UniformityInfo` (computed only after
; every cycle has been fully processed) looks at it -- but, before this
; fix, `inner.header`'s own exit branch was left exactly as `linearizeCycle`
; found it (a plain `CondBrInst`, never converted into the masked,
; uniform-backedge shape a genuinely divergent header check gets), so
; `feme-cpu-simdize` correctly, but too late, rejected it as an unremoved
; divergent branch.
;
; The fix predicts this staleness ahead of time
; (`dependsOnResourceOrImageLoadInCycle`'s new, opt-in
; `AlsoCheckAncestorCycles` parameter) and folds `inner.header`'s own exit
; decision into its loop-carried mask immediately, exactly as a
; genuinely-`HeaderDivergent` header's own exit check already is: no
; iteration ever really leaves `inner.header` early any more (`inner.body`
; is now unconditionally reached every time `inner.header` is), and the
; real per-lane "should this lane's own iterations keep happening"
; decision is instead folded into `inner.latch`'s own backedge, gated by
; a uniform `feme.cpu.mask.any` reduction over the per-lane mask -- see
; `closeLatch`'s own comment for why this, not a second conditional exit
; edge out of `inner.header` itself, is what keeps `inner.latch`'s
; backedge itself uniform control flow.

; CHECK-LABEL: define void @main(
; CHECK: outer.merge:
; CHECK: %bound = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 %live.merge)
; CHECK: inner.header:
; CHECK: %inner.cond = icmp slt i32 %inner.j, %bound
; CHECK: %active.header.live = and i1 %active.live, %inner.cond
; CHECK: %active.header.sideeffect = and i1 %active.sideeffect, %inner.cond
; CHECK-NOT: br i1 %inner.cond
; CHECK: br label %inner.body
; CHECK: inner.body:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 2, i64 0, i32 1, i1 %active.header.sideeffect)
; CHECK: inner.latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header.live)
; CHECK: br i1 %loop.any.active, label %inner.header, label %after.inner
define void @main(i32 %n, ptr %p, ptr %heap, i32 %heap_count) #0 {
entry:
  br label %outer.header
outer.header:
  %outer.i = phi i32 [0, %entry], [%outer.i.next, %outer.latch]
  %outer.cond = icmp slt i32 %outer.i, %n
  br i1 %outer.cond, label %outer.body, label %exit
outer.body:
  %old = atomicrmw add ptr %p, i32 1 seq_cst
  %outer.discard.cond = icmp ult i32 %old, 32
  br i1 %outer.discard.cond, label %outer.then, label %outer.else
outer.then:
  call void @feme.stage.discard(i1 true)
  br label %outer.merge
outer.else:
  br label %outer.merge
outer.merge:
  %bound = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 true)
  br label %inner.header
inner.header:
  %inner.j = phi i32 [0, %outer.merge], [%inner.j.next, %inner.latch]
  %inner.cond = icmp slt i32 %inner.j, %bound
  br i1 %inner.cond, label %inner.body, label %after.inner
inner.body:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 2, i64 0, i32 1, i1 true)
  br label %inner.latch
inner.latch:
  %inner.j.next = add i32 %inner.j, 1
  br label %inner.header
after.inner:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 1, i64 0, i32 1, i1 true)
  br label %outer.latch
outer.latch:
  %outer.i.next = add i32 %outer.i, 1
  br label %outer.header
exit:
  ret void
}
declare void @feme.stage.discard(i1)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
