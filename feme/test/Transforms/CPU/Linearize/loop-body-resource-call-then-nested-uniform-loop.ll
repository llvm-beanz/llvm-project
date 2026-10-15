; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap K6z: `feme::cpu::DiamondFlattener::flattenLoopBodyRegion`'s own
; `validate` walk (called from `LoopLinearizer::linearizeCycle`'s "fully
; uniform loop with body mask ops" branch) previously tolerated exactly
; one nested-cycle-boundary shape: a cycle whose own branch reaches the
; region's `End` *directly* (the loop's own exit check sitting at the very
; tail of the body region, e.g. `loop-relay-chain-two-hops.ll`'s shape).
; A genuinely separate, already fully-attempted nested sibling loop
; sitting *earlier* in the body -- with real straight-line code (here,
; `after.inner`'s own resource call) between its exit and `End` -- was
; always rejected instead, with no diagnostic distinguishing it from any
; other truly-unsupported shape: `"loop at 'loop' has an internal branch
; in a body shape this milestone does not yet lower"`.
;
; This reduces `matrix_single_subscript_load.test`'s own shape (an outer
; `for (i...)` loop with a uniform `i == 0` diamond in its body -- needing
; a `feme.cpu.resource.*` call's mask threaded through it -- then an
; entirely separate, uninvolved nested `for (j...)` loop before the outer
; loop's own latch) down to its essential structure: the nested loop here
; has no mask-affecting operation inside its own body at all (so
; `LoopLinearizer::linearizeCycle` leaves it completely alone, post-order,
; before the outer loop is even attempted -- see
; `linearizeCyclePostOrder`), yet its mere *presence*, structurally
; between the outer diamond and the outer loop's own latch, used to be
; enough to make the whole outer region unsupported.
;
; The fix lets `validate`/`flatten` recognize any cycle header strictly
; nested inside the cycle currently being linearized (guaranteed already
; fully attempted by post-order, whether or not it needed any masking of
; its own) as transparent: the walk continues from that nested cycle's
; own single exit block onward to `End`, instead of stopping at its
; header. `after.inner` is reached (structurally past the nested cycle,
; still ahead of the outer loop's own `latch`) exactly as if the nested
; loop were not there at all, and the whole region linearizes cleanly
; with no diagnostic -- instead of being unconditionally rejected as an
; unsupported shape, as it was before this fix.
;
; Both resource calls' own mask operands are left as the literal `i1
; true` they started with (not narrowed to some derived `i1` value):
; this outer loop has no `feme.stage.discard`/`.demote` anywhere in its
; body (the only operations that ever make `DiamondFlattener::flatten`'s
; `Masks.SideEffect` genuinely diverge from `true` -- see
; `unsupported-stage-op-in-uniform-loop.ll`, which diagnoses exactly that
; shape as unsupported instead), so every lane that structurally reaches
; either call is unconditionally active; `isKnownConstantMask`'s
; `lookThroughTrivialPhi` correctly recognizes this and intentionally
; leaves both calls' masks untouched rather than rewriting them to a
; needlessly non-constant phi. This test's real assertion is therefore
; purely structural (reaches `after.inner` and `latch` at all, matching
; the shape above, instead of diagnosing or crashing) -- a separate,
; divergence-bearing variant would be needed to additionally exercise
; genuine mask narrowing across this same nested-cycle-boundary
; walk-through, which is not what `K6z` itself is about.

; CHECK-LABEL: define void @main(
; CHECK: then:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
; CHECK: inner.header:
; CHECK: br i1 %inner.cond, label %inner.header, label %after.inner
; CHECK: after.inner:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 1, i32 2, i1 true)
; CHECK: br label %latch
; CHECK: latch:
; CHECK: br label %loop
define void @main(i32 %n, ptr %heap, i32 %heap_count) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %loop.cond = icmp slt i32 %i, %n
  br i1 %loop.cond, label %body, label %exit
body:
  %cond = icmp eq i32 %i, 0
  br i1 %cond, label %then, label %else
then:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  br label %diamond.merge
else:
  br label %diamond.merge
diamond.merge:
  br label %inner.header
inner.header:
  %j = phi i32 [0, %diamond.merge], [%j.inc, %inner.header]
  %j.inc = add i32 %j, 1
  %inner.cond = icmp slt i32 %j, %n
  br i1 %inner.cond, label %inner.header, label %after.inner
after.inner:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 1, i32 2, i1 true)
  br label %latch
latch:
  %inc = add i32 %i, 1
  br label %loop
exit:
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
