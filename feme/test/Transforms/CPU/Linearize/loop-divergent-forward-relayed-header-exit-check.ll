; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(9a)/ENV6(c)(10): reduced (via `llvm-reduce`, with an
; interestingness test requiring the exact "only a uniform pass-through
; region from the header's own exit check to the latch" diagnostic text
; -- a looser, generic "has an internal branch in a body shape" match
; converges on an unrelated bug instead, see `.instructions.md`'s own
; `llvm-reduce` lesson) from the real `dEQP-VK.graphicsfuzz.
; cosh-return-inf-unused` shader. The real shape traces as 4 nested
; cycles (see `VulkanCTSReport.md`'s `L545` writeup), but none of that
; nesting is actually load-bearing: this single, non-nested loop
; reproduces the identical diagnostic standalone (before the
; `ENV6(c)(10)` fix below).
;
; Root cause, traced with `FEME_DEBUG_LINEARIZE_TRACE=1`/
; `FEME_DEBUG_PASSTHROUGH=1`: `Flow18._crit_edge`'s own terminator below
; -- `br i1 %0, label %Flow22, label %Flow22` -- is a syntactically
; valid but semantically degenerate conditional branch with *identical*
; successors on both arms, almost certainly an `llvm-reduce` reduction
; artifact (the real, non-reduced shader is most unlikely to ever emit
; this shape) rather than something present in the original shader.
; `uniformRelayChain` (the free function backing both
; `recoverRelayExitCheck` and the more general `matchExitCheckWithRelay`)
; has no notion of "stop once a real exit is found" -- it keeps walking
; every reachable block looking for a second, independent path, and
; unconditionally bails out the moment it encounters *any* divergent
; `CondBr` anywhere in that reachable set, including this one. That
; caused `findForwardExitCheckBlock`'s own candidate (the loop's real
; exit check, split off of `Header` by an earlier Roadmap `ENV6(c)(4)`
; transform into a fresh `.body` block) to never be recognized as a
; relayed exit check at all -- not merely rejected for being divergent,
; which is a different failure mode than the one this test's name/
; original comment assumed was blocking it. With no recognized
; `HeaderExit`, `BodyRegionStart` stayed `Header`, and
; `collectUniformPassThroughRegion`'s walk reached `.body` as an
; ordinary (not header-exit-recognized) `CondBr`, correctly rejecting it
; as "just another divergent branch in the body" -- the diagnostic this
; test originally pinned, for the wrong structural reason: `.body` is
; actually this loop's own real exit check, not a second, independent
; divergent branch.
;
; Fixed by `foldConstantConditionBranches` (this file's neighbor
; `hasStageMaskOps`'s own comment describes its original, narrower
; "literal constant condition" scope) now also folding an identical-
; successor `CondBr` regardless of whether its own condition is a
; literal constant -- `llvm::ConstantFoldTerminator` itself already
; recognizes and folds that exact shape unconditionally (see its own
; "Conditional branch to same location?" case), independent of the
; condition's constancy, so this only needed to stop gating the call to
; it on an unrelated check. Run once, up front, before any of this
; pass's own cycle/divergence analysis, this removes the degenerate
; branch entirely before `findForwardExitCheckBlock`'s chase ever has a
; chance to trip over it, letting the loop's real exit check be
; recognized and relayed exactly as designed with no change needed to
; the forward-match divergence guard itself (Roadmap `L432(c)`/`L462`,
; in `LoopLinearizer::linearizeCycle`) -- an earlier attempt this
; milestone to instead loosen that guard regressed two other pinned
; shapes (`loop-check-separate-carried-value.ll` and the
; `DoesNotDoubleClaimSoleConditionalBranchAsBothHeaderAndCheckExit` unit
; test) and was reverted in favor of this fix.

; CHECK-LABEL: define void @main(
; CHECK: br label %.body
; CHECK: .body:
; CHECK: br label %Flow18._crit_edge
; CHECK: Flow22:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %live.merge)
; CHECK: Flow18._crit_edge:
; CHECK-NEXT: %6 = call float @feme.stage.input.load.f32()
; CHECK-NEXT: br label %Flow22
; CHECK: loop.exit.guard:
; CHECK-NEXT: call void @feme.cpu.stage.return.masks.i1(i1 true, i1 true)
; CHECK-NEXT: ret void

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

declare float @feme.stage.input.load.f32()

declare void @feme.stage.discard(i1)

define void @main(i1 %0) #0 {
  br label %2

2:                                                ; preds = %Flow22._crit_edge, %1
  %feme.stage.output.shadow19.0 = phi float [ 0.000000e+00, %1 ], [ %5, %Flow22._crit_edge ]
  %3 = fptosi float %feme.stage.output.shadow19.0 to i32
  %4 = icmp sgt i32 %3, 0
  br i1 %4, label %Flow18._crit_edge, label %Flow22

Flow22:                                           ; preds = %Flow18._crit_edge, %Flow18._crit_edge, %2
  %5 = phi float [ 0.000000e+00, %2 ], [ %6, %Flow18._crit_edge ], [ %6, %Flow18._crit_edge ]
  br i1 %0, label %loop.exit.guard, label %Flow22._crit_edge

Flow22._crit_edge:                                ; preds = %Flow22
  br label %2

Flow18._crit_edge:                                ; preds = %2
  call void @feme.stage.discard(i1 true)
  %6 = call float @feme.stage.input.load.f32()
  br i1 %0, label %Flow22, label %Flow22

loop.exit.guard:                                  ; preds = %Flow22
  ret void
}

attributes #0 = { "feme.shader.stage"="fragment" }
