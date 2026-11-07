; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap ENV6(c)(9a): reduced (via `llvm-reduce`, with an
; interestingness test requiring the exact "only a uniform pass-through
; region from the header's own exit check to the latch" diagnostic text
; -- a looser, generic "has an internal branch in a body shape" match
; converges on an unrelated bug instead, see `.instructions.md`'s own
; `llvm-reduce` lesson) from the real `dEQP-VK.graphicsfuzz.
; cosh-return-inf-unused` shader. The real shape traces as 4 nested
; cycles (see `VulkanCTSReport.md`'s `L545` writeup), but none of that
; nesting is actually load-bearing: this single, non-nested loop
; reproduces the identical diagnostic standalone.
;
; Root cause, traced with `FEME_DEBUG_LINEARIZE_TRACE=1`/
; `FEME_DEBUG_PASSTHROUGH=1`: an earlier Roadmap `ENV6(c)(4)` transform
; splits this loop's own `Header` into a trivial phi-only block plus a
; fresh `.body` block holding `Header`'s real, genuinely-divergent exit
; check (its condition depends on a resource load, which this pass's
; `UniformityInfo` already marks divergent directly, independent of any
; masking). `matchExitCheck`/`recoverRelayExitCheck` never match this
; split-off `.body` as `Header`'s own exit check since it is no longer
; `Header`'s own terminator, so `findForwardExitCheckBlock` is tried
; instead -- but its caller (around `LoopLinearizer::linearizeCycle`'s
; own "Roadmap L432(c)"/"Roadmap L462" comments) deliberately refuses any
; *divergent* forward match, to avoid a known crash where `CheckBlock`'s
; own widening logic later claims and mutates the same block a second
; time. With no recognized `HeaderExit` at all, `BodyRegionStart` stays
; `Header`, and `collectUniformPassThroughRegion`'s walk reaches `.body`
; as an ordinary (not header-exit-recognized) `CondBr` and correctly
; rejects it as "just another divergent branch in the body" -- exactly
; the diagnostic below, but for the wrong structural reason: `.body` is
; actually this loop's own real exit check, not a second, independent
; divergent branch.
;
; Not yet fixed: `L462`'s comment documents that removing the "divergent
; forward match" restriction entirely was tried once before and caused a
; real crash (a double-mutation of the same recovered block), so a safe
; fix needs to teach the forward-match path to recognize this specific
; "split-off `Header`" shape as its own case, not simply drop the
; existing guard. Left as a reduced, pinned anchor for whoever attempts
; that next.

; CHECK: error: feme-cpu-linearize: function 'main': loop at '{{.*}}' has an internal branch in a body shape this milestone does not yet lower; only a uniform pass-through region from the header's own exit check to the latch is supported yet (roadmap milestone 6 deviation)
; CHECK-NEXT: error: feme-cpu-linearize: function 'main': calls a mask-affecting feme.stage.* operation ('discard'/'demote'/'is_helper') in a shape this milestone does not lower (e.g. inside an otherwise uniform loop)

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
