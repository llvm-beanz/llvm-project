; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(3)/ENV6(c)(5)/ENV6(c)(9): reduced (via llvm-reduce, using
; an interestingness test that greps for a CHECK line this shape used to
; produce) from the real `dEQP-VK.graphicsfuzz.cosh-return-inf-unused`
; shader, part of the "only a uniform pass-through region" cluster's own
; ENV6(c)(5) header-exit-to-latch sub-bucket (see VulkanCTSReport.md's
; nested-loop triage writeup for the full investigation). Block names are
; kept as `llvm-reduce` emitted them (not hand-renamed) because this exact
; relay shape -- the outer loop's own exit check reached only through a
; `Flow11`/`Flow14._crit_edge` relay rather than a direct two-successor
; branch -- is itself part of what originally defeated the pass's
; exit-check/guard recognition; hand-simplifying the block structure
; previously produced a different (also-unsupported, but not the
; intended) diagnostic.
;
; `Flow12` is a genuinely divergent *inner* self-loop (its own exit check
; depends on a `feme.stage.input.load.f32` resource load) nested inside
; the outer loop (header `%1`, latch-relay `Flow11`/`Flow14._crit_edge`).
; `loop.exit.guard5` conditionally reaches block `%6`, which calls
; `feme.stage.discard`, before rejoining the outer loop's own exit-check
; relay at `Flow11`.
;
; Roadmap ENV6(c)(9) (this fix): `LoopLinearizer::linearizeCycle`'s
; `BodyFlattenOnly` fallback -- which defers to
; `DiamondFlattener::flattenLoopBodyRegion` (already able to recognize
; `loop.exit.guard5` as belonging to the already-linearized child cycle
; `Flow12`, via the existing `GetAlreadyLinearizedCycleExitForBody`
; mechanism, and mask `%6`'s `discard` call accordingly) whenever
; `collectUniformPassThroughRegion` itself cannot walk straight through --
; used to require `HeaderDivergent` (the *outer* loop's own exit check
; being divergent), even though nothing about the fallback or
; `DiamondFlattener` actually needs that. This repro's outer loop has a
; plain *uniform* header (`%1` branches unconditionally to `Flow12`), so
; the fallback was never even attempted, leaving this shape permanently
; unsupported. Removing that unnecessary restriction fixes this repro
; with no new exemption logic: `DiamondFlattener`'s existing machinery was
; already sufficient once given the chance to run.
;
; Verified via: this test's own `verify`-clean output below; a full
; `check-feme` run (no other regression); and a full 757-case
; graphicsfuzz CTS sweep (byte-identical Pass/Fail lists before and after
; this change -- this specific *2-deep* shape does not otherwise occur in
; that corpus). The real `cosh-return-inf-unused` shader is 4 cycles deep
; and still fails after this fix, against an earlier/different cycle
; layer -- see `VulkanCTSReport.md`'s `L544` entry for the follow-on.

; CHECK-NOT: call void @feme.stage.discard
; CHECK: loop.exit.guard5:
; CHECK-NEXT: %not.Guard.6.inv = xor i1 %4, true
; CHECK-NEXT: %live.t = and i1 %active.live1, %4
; CHECK-NEXT: %sideeffect.t = and i1 %active.sideeffect2, %4
; CHECK-NEXT: %live.f = and i1 %active.live1, %not.Guard.6.inv
; CHECK-NEXT: %sideeffect.f = and i1 %active.sideeffect2, %not.Guard.6.inv

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
