; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L425: `DiamondFlattener`'s whole-function `MergeVisited` set is
; deliberately shared across every `flattenLoopBodyDiamond`/
; `flattenLoopBodyRegion` call (see `loop-body-resource-call-then-nested-
; uniform-loop.ll`'s own `L375`/`K6z` doc comment) so two still-pending,
; independent flatten attempts can never double-claim the same block.
; That protection used to also misfire on a case that is not a genuine
; double claim at all: `LoopLinearizer::linearizeCycle`'s own early
; "ordinary nested diamond" fixed-point loop first flattens `header`'s
; body's own inner `%eq` diamond (divergent, since it compares the
; uniform induction variable against a per-lane
; `llvm.spv.thread.id.in.group` value), inserting its blocks into
; `MergeVisited` as an unavoidable side effect of `validate`. The *later*,
; separate `flattenLoopBodyRegion` call -- needed here because `header`'s
; own loop-exit check and its backedge decision are fused into a single
; `Flow`-style dispatch block (the `StructurizeCFG` idiom this reduces
; from `Feature/HLSLLib/AllMemoryBarrierWithGroupSync.test`'s own
; pre-linearize IR) -- must walk the *entire* loop body region to reach
; its own `Latch`, which structurally still contains the diamond already
; resolved by the earlier call. `validate` used to see `header`'s inner
; diamond's blocks already present in `MergeVisited` and spuriously
; diagnose this walk-through as a conflict: `"loop at '' has an internal
; branch in a body shape this milestone does not yet lower"`, even though
; the diamond was never contested -- it was already fully committed by an
; earlier, independent, already-*returned* call.
;
; The fix adds a `ResolvedBlocks` set (same whole-function lifetime as
; `MergeVisited`) recording every block actually committed by a
; fully-successful top-level flatten call. `validate`'s conflict check
; becomes "fail only if seen in `MergeVisited` but *not* yet in
; `ResolvedBlocks`" -- preserving the original double-claim protection
; for any still-in-progress/uncommitted candidate while permitting safe
; pass-through of a diamond some *earlier*, already-finished call fully
; resolved.
;
; This also exercises genuine mask narrowing (unlike the `K6z` test
; above, whose nested loop had no masked operation of its own): the
; `then` arm's `feme.cpu.resource.store.raw.i32` call's mask must be
; narrowed from a bare `i1 true` to the inner diamond's own per-lane
; `%eq`-derived select chain, confirming the pass-through doesn't just
; silently skip re-threading the already-resolved diamond's own mask.

; CHECK-LABEL: define void @main(
; CHECK: body:
; CHECK: %eq = icmp eq i32 %gtid, %i
; CHECK: %sideeffect.t = and i1 true, %eq
; CHECK: then:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 %sideeffect.t)
; CHECK: body.merge:
; CHECK: br label %latch
; CHECK: latch:
; CHECK: br label %Flow._crit_edge
; CHECK: Flow._crit_edge:
; CHECK: br label %header
declare i32 @llvm.spv.thread.id.in.group.i32(i32) #0

define void @main(ptr %heap, i32 %heap_count) #1 {
entry:
  %gtid = call i32 @llvm.spv.thread.id.in.group.i32(i32 0)
  br label %header

header:
  %i = phi i32 [0, %entry], [%i.next, %Flow._crit_edge]
  %cond = icmp ult i32 %i, 512
  br i1 %cond, label %body, label %.Flow_crit_edge

.Flow_crit_edge:
  br label %Flow

body:
  %eq = icmp eq i32 %gtid, %i
  br i1 %eq, label %then, label %body.else

then:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  br label %body.merge

body.else:
  br label %body.merge

body.merge:
  br label %latch

latch:
  %i.next = add i32 %i, 1
  br label %Flow

Flow:
  %p = phi i1 [ false, %latch ], [ true, %.Flow_crit_edge ]
  br i1 %p, label %exit, label %Flow._crit_edge

Flow._crit_edge:
  br label %header

exit:
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { nounwind willreturn memory(none) }
attributes #1 = { "feme.cpu.wavesize"="4" "hlsl.shader"="compute" "hlsl.numthreads"="512,1,1" }
