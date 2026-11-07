; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap ENV6(c)(3)/ENV6(c)(5): reduced (via llvm-reduce, using an
; interestingness test that greps for the first CHECK line below) from
; the real `dEQP-VK.graphicsfuzz.cosh-return-inf-unused` shader, part of
; the "only a uniform pass-through region" cluster's own ENV6(c)(5)
; header-exit-to-latch sub-bucket (see VulkanCTSReport.md's nested-loop
; triage writeup for the full investigation, including a confirmed-unsafe
; generalization attempt that is *not* landed here). Block names are kept
; as `llvm-reduce` emitted them (not hand-renamed) because this exact
; relay shape -- the outer loop's own exit check reached only through a
; `Flow11`/`Flow14._crit_edge` relay rather than a direct two-successor
; branch -- is itself part of what defeats the pass's exit-check/guard
; recognition; hand-simplifying the block structure previously produced a
; different (also-unsupported, but not the intended) diagnostic.
;
; `Flow12` is a genuinely divergent *inner* self-loop (its own exit check
; depends on a `feme.stage.input.load.f32` resource load) nested inside
; the outer loop (header `%1`, latch-relay `Flow11`/`Flow14._crit_edge`).
; `loop.exit.guard5` conditionally reaches block `%6`, which calls
; `feme.stage.discard`, before rejoining the outer loop's own exit-check
; relay at `Flow11`. Neither `LoopLinearizer::collectUniformPassThroughRegion`
; (the outer loop's body-region walk) nor the `Pre` `DiamondFlattener`
; feasibility probe it falls back to has any notion that `loop.exit.guard5`
; and `%6`'s branches belong to an already-linearized *child* cycle rather
; than a second, unsupported divergent branch of the outer loop's own body;
; both reject the shape as soon as they walk into it.
;
; Roadmap ENV6(c)(9): `%6`'s `feme.stage.discard` call is given an explicit
; `i1 true` condition argument (hand-restored after `llvm-reduce` dropped
; it, since the shape it reduced away never reaches `applyStageMasks` --
; the real calling convention, confirmed against every other
; `feme.stage.discard` lit test in this directory, always takes one) so
; that any future fix which gets far enough to actually flatten/mask this
; region (rather than rejecting it up front, as today) does not immediately
; hit an unrelated `CallBase::getArgOperand` out-of-bounds assertion in
; `applyStageMasks`'s own `Discard` case purely from this test's own
; otherwise-untested, reduced-away argument list.

; CHECK: feme-cpu-linearize: function 'main': loop at '' has an internal branch in a body shape this milestone does not yet lower; only a uniform pass-through region from the header's own exit check to the latch is supported yet (roadmap milestone 6 deviation)
; CHECK: feme-cpu-linearize: function 'main': calls a mask-affecting feme.stage.* operation ('discard'/'demote'/'is_helper') in a shape this milestone does not lower (e.g. inside an otherwise uniform loop)

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

declare float @feme.stage.input.load.f32()

declare void @feme.stage.discard(i1)

define void @main(i1 %0) #0 {
Flow19:
  br label %1

1:                                                ; preds = %Flow14._crit_edge, %Flow19
  br label %Flow12

2:                                                ; preds = %Flow11
  ret void

Flow14._crit_edge:                                ; preds = %Flow11
  br label %1

Flow11:                                           ; preds = %loop.exit.guard5, %6
  br i1 %0, label %2, label %Flow14._crit_edge

Flow12:                                           ; preds = %Flow12, %1
  %3 = phi i1 [ false, %Flow12 ], [ false, %1 ]
  %4 = call float @feme.stage.input.load.f32()
  %5 = fcmp uge float %4, 0.000000e+00
  br i1 %5, label %loop.exit.guard5, label %Flow12

6:                                                ; preds = %loop.exit.guard5
  call void @feme.stage.discard(i1 true)
  br label %Flow11

loop.exit.guard5:                                 ; preds = %Flow12
  %Guard.6.inv = xor i1 %3, false
  br i1 %Guard.6.inv, label %6, label %Flow11
}

attributes #0 = { "feme.shader.stage"="fragment" }
