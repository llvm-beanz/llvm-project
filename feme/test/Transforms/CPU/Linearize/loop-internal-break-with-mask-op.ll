; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L419(h): a loop whose header holds the real (uniform) trip-count
; check, with a *separate*, later block in the body holding its own
; divergent `break` check that branches straight to the loop's own exit
; block -- distinct from `loop-break.ll`'s supported shape, where the
; divergent check is fused directly into the header itself (so
; `LoopLinearizer::linearizeCycle`'s `HeaderDivergent`/`stayInLoop` path
; handles it) and `loop-break-structurized.ll`'s supported shape, where the
; break's own reconvergence block is a pure relay the loop's real exit
; check sits just past (so `DiamondFlattener::validate`'s
; `isLoopControlOrHeaderRelayEdge` chase recognizes it).
;
; This exact shape -- a body block's own branch reaching the *current*
; cycle's real exit block directly, with no relay chain to chase through --
; is already supported when the loop body has no mask-affecting operation
; of its own (see `loop-break-separate-from-header-check.ll`, which is
; identical but for the `feme.cpu.resource.store.raw.i32` call below): it
; goes through `LoopLinearizer::linearizeCycle`'s simpler, non-masked
; `collectUniformPassThroughRegion`/`exited.via.check` path, which already
; tracks a body-level break correctly. Previously, the moment the body
; also contained a mask-affecting call (here, an ordinary resource store,
; any of the `StageOpKind`s `cycleHasMaskOps` also recognizes would
; trigger the same gap), `linearizeCycle` instead took its "fully uniform
; loop with body mask ops" branch, built on
; `DiamondFlattener::canFlattenLoopBodyRegion`/`flattenLoopBodyRegion`
; over the `[HeaderStayInLoop, Latch)` region -- and that path's own
; `validate` walk classified the body block's branch to the loop's exit
; as a cycle-boundary edge (`isLoopControlEdge`'s `!CI.contains(C,
; Target)` test, true for the cycle's own real exit block exactly as it
; is for a different, nested child cycle's own control edge) and deferred
; it as "LoopLinearizer's problem, not an error" -- except that call *is*
; `LoopLinearizer`, already in the middle of processing this exact cycle,
; with no other call left to defer to. `canFlattenLoopBodyRegion` then
; failed because `CycleBoundaryBlocks` was non-empty, and the body's own
; break was left entirely unhandled.
;
; Fixed by widening `RealExitCandidates` (when still empty after the
; ordinary `DivergentCandidates` scan) to also accept a uniform
; `OtherCondBrBlocks` member whose own `matchExitCheckWithRelay` resolves
; to this cycle's `ExitBlock`, gated on both `cycleHasMaskOps(C)` (no
; reason to widen a loop with nothing to mask) and
; `OtherCondBrBlocks.size() == 1` (the precise discriminator -- see
; `Linearize.cpp`'s own widening comment -- between this single-check
; shape and a `switch`-lowered multi-diamond body like
; `mixed_break_continue_fragment`'s own, whose extra internal branching
; must still defer to the pre-existing fallback path). `body` itself
; becomes the widened `CheckBlock`, and the loop-carried
; `active.live`/`active.sideeffect` masking machinery `HeaderDivergent`'s
; own `stayInLoop` already gives the header's *own* check threads through
; it exactly the same way.
;
; The resource call's own mask operand is, for now, still left at the
; literal `i1 true` it started with, rather than narrowed to
; `active.sideeffect` as would be strictly correct (a lane that already
; exited via an earlier iteration's check should not still perform this
; call's side effect on every later iteration -- a real, pre-existing,
; still-open latent masking gap, not introduced by this change).
; `LoopLinearizer::makeActivePNPair`'s `active.live`/`active.sideeffect`
; phis only have their non-cycle-predecessor incoming value populated by
; the time `applyStageMasks` first runs on a `CheckBlock` that is itself
; `Header` (this widening's own target shape) -- `addLatchIncoming` only
; fills in the real `Latch` value much later, once the whole cycle's
; exit check(s) are linearized -- so `isKnownConstantMask`'s
; `lookThroughTrivialPhi` sees a single-incoming-value phi and (wrongly)
; treats it as the trivial constant `true`. A seeded-poison-placeholder
; fix for this was attempted and found to independently fix this gap,
; but it also reintroduced a *different* regression in
; `dEQP-VK.glsl.loops.special.*select_iteration_count*`
; (`DiamondFlattener::flattenLoopBodyDiamond`'s own early fixed-point
; pre-pass judged a loop-header-internal if/else built from a resource
; load's result "uniform" from a stale, not-yet-masked snapshot, and
; never revisited that judgment once the mask legitimately became
; non-constant). Roadmap `L419(w)` lands the complementary fix for that
; regression (`LoopLinearizer::flattenTaintedBranchesInRegion` +
; `DiamondFlattener::flattenKnownDivergentBranch`, re-scanning and
; physically flattening any branch inside a `PreRegion`/`PostRegion` that
; only became mask-dependent once `applyStageMasks` ran, including one
; sitting at the cycle's own header), letting the poison-placeholder fix
; finally land alongside it with no regression -- see that fix's own
; roadmap entry and `agent_thoughts.md`'s matching session writeup for
; the full mechanism. The resource store below is now correctly masked
; by `active.sideeffect`, closing this gap.
;
; This reduces `dEQP-VK.glsl.loops.special.while_uniform_iterations.
; conditional_break_{fragment,vertex}` (and the identically-shaped
; `for_uniform_iterations.conditional_break_*`/`do_while_uniform_iterations.
; conditional_break_*`) down to its essential structure.

; Roadmap L419(w): the body's own break check below is deliberately keyed
; off `%k`, a value *unrelated* to the header's own `%inc`/`%n` trip-count
; check, rather than (as an earlier version of this test had it) a second
; comparison between `%inc` and `%n` themselves. That earlier choice was
; an accidental self-contradiction: since `body` is only ever reached once
; `%loop.cond = icmp slt i32 %inc, %n` has already been proven true on the
; edge into it, `icmp eq i32 %inc, %n` is *always* false there by plain
; flow-sensitive single-predecessor reasoning alone (confirmed with `opt
; -passes=instsimplify` in isolation, no cycle/masking machinery
; involved) -- entirely independent of whether this test's own masking
; fix is correct, LLVM's own instruction simplification legitimately
; folds that particular `icmp` away to `i1 false`, which then cascades
; into folding away the very masking machinery this test exists to
; exercise. Comparing `%inc` against an unrelated opaque value keeps the
; break check genuinely data-dependent so the masking survives to be
; checked below.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.check.live, %latch ]
; CHECK: %active.sideeffect = phi i1 [ true, %entry ], [ %active.check.sideeffect, %latch ]
; CHECK: body:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 %active.sideeffect)
; CHECK: %break.cond = icmp sgt i32 %inc, %k
; CHECK: %active.check.live = and i1 %active.live,
; CHECK: %active.check.sideeffect = and i1 %active.sideeffect,
; CHECK: br label %latch
; CHECK: latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK: br i1 %loop.any.active, label %header, label %exit
define void @main(ptr %heap, i32 %heap_count, i32 %n, i32 %k) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %body, label %exit
body:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  %break.cond = icmp sgt i32 %inc, %k
  br i1 %break.cond, label %exit, label %latch
latch:
  br label %header
exit:
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
