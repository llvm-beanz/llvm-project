; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap ENV6(c)(11): reduced (via `llvm-reduce`, with an interestingness
; test requiring the rejected block's own *terminator* to branch on a
; `Guard.N.inv` value specifically -- a looser test that only checks for
; the shared "has an internal branch in a body shape" diagnostic text, or
; even one that merely checks a `Guard.N.inv` definition is present
; anywhere in the function, converges on an unrelated, simpler "phi of
; constants misclassified as divergent" shape instead, see
; `feme/.instructions.md`'s `llvm-reduce` lessons -- this repro
; independently reconfirmed that exact gotcha while being built) from the
; real `dEQP-VK.graphicsfuzz.cosh-return-inf-unused` shader, after the
; `ENV6(c)(10)` fix. Also needed its `feme.stage.discard()` call restored
; to `feme.stage.discard(i1 true)` by hand post-reduction (the
; `llvm-reduce`-drops-stage-op-arguments gotcha, same file).
;
; `ENV6(c)(10)`'s fix cleared the *first* obstacle for this shader
; (`.body`'s relayed exit check, a resource-load-derived condition, is now
; correctly recognized and widened). This file pins the *second*,
; genuinely divergent branch found one layer further in:
; `loop.exit.guard3`'s own `%Guard.4.inv = xor i1 %7, false` branch, where
; `%7` is itself a reconvergence phi of the standard SPIR-V-structurizer
; "did an earlier `if` execute" pattern (a 2-predecessor phi of literal
; `false`/`false` here, reduced from the real shader's 3-predecessor
; `true`/`false`/`false` -- see `VulkanCTSReport.md`'s `L546` writeup).
; `collectUniformPassThroughRegion` walks from the loop's own (now
; correctly recognized) `HeaderExit` toward the latch, reaches
; `loop.exit.guard3` as an ordinary block inside that walk, and correctly
; rejects it as a second, independent divergent `CondBr` -- the documented
; "two divergent checks in one pass-through region" limitation this
; milestone has never supported (see that function's own comment on this
; rejection branch in `Linearize.cpp`). Fixing this needs
; `DiamondFlattener`-style flattening of this *second* divergent diamond
; from within the pass-through walk itself, not merely another narrow
; IR-canonicalization fix like `ENV6(c)(10)`'s -- not yet designed; this
; file exists to pin the exact shape and diagnostic as a starting point,
; not as a regression test for a landed fix.

; CHECK: feme-cpu-linearize: function 'main': loop at '' has an internal branch in a body shape this milestone does not yet lower; only a uniform pass-through region from the header's own exit check to the latch is supported yet (roadmap milestone 6 deviation)
; CHECK: feme-cpu-linearize: function 'main': calls a mask-affecting feme.stage.* operation ('discard'/'demote'/'is_helper') in a shape this milestone does not lower (e.g. inside an otherwise uniform loop)

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

declare float @feme.stage.input.load.f32()

declare void @feme.stage.discard(i1)

define void @main() #0 {
  br label %1

1:                                                ; preds = %Flow22._crit_edge, %0
  %feme.stage.output.shadow19.0 = phi float [ 0.000000e+00, %0 ], [ %4, %Flow22._crit_edge ]
  %2 = fptosi float %feme.stage.output.shadow19.0 to i32
  %3 = icmp sge i32 %2, 0
  br i1 %3, label %Flow18, label %.Flow22_crit_edge

.Flow22_crit_edge:                                ; preds = %1
  br label %Flow22

Flow22:                                           ; preds = %Flow17, %.Flow22_crit_edge
  %4 = phi float [ 0.000000e+00, %.Flow22_crit_edge ], [ %6, %Flow17 ]
  %5 = phi i1 [ true, %.Flow22_crit_edge ], [ true, %Flow17 ]
  br i1 %5, label %loop.exit.guard, label %Flow22._crit_edge

Flow22._crit_edge:                                ; preds = %Flow22
  br label %1

Flow17:                                           ; preds = %loop.exit.guard3, %10
  %6 = call float @feme.stage.input.load.f32()
  br label %Flow22

Flow18:                                           ; preds = %Flow18, %1
  %7 = phi i1 [ false, %Flow18 ], [ false, %1 ]
  %8 = call float @feme.stage.input.load.f32()
  %9 = fcmp uge float %8, 0.000000e+00
  br i1 %9, label %loop.exit.guard3, label %Flow18

10:                                               ; preds = %loop.exit.guard3
  call void @feme.stage.discard(i1 true)
  br label %Flow17

loop.exit.guard:                                  ; preds = %Flow22
  ret void

loop.exit.guard3:                                 ; preds = %Flow18
  %Guard.4.inv = xor i1 %7, false
  br i1 %Guard.4.inv, label %10, label %Flow17
}

attributes #0 = { "feme.shader.stage"="fragment" }
