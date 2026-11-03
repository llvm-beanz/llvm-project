; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV7(f): reduced (via llvm-reduce) from a real, reproduced
; `dEQP-VK.graphicsfuzz.stable-colorgrid-modulo-vec3-values-from-matrix`
; residual-image failure (no crash, just a wrong pixel value: the real
; shader has `if (c[i] >= 1.0) { c[i] *= c[i]; if (fragcoord.y < 0.0)
; break; }` inside its main loop). Block `3`'s `fcmp` here looks uniform
; to `UniformityInfo` (computed before any masking), since it only reads
; a plain, not-yet-masked `load` -- so `collectUniformPassThroughRegion`
; happily includes block `3` *and* its "true" arm (block `7`, containing
; this reduction's stand-in side effect, the `store`) in `PreRegion`.
; `LoopLinearizer::linearizeCycle`'s blanket `applyStageMasks` loop over
; the whole of `PreRegion` then masks block `7`'s `store` with the
; loop's own coarse `active.sideeffect` mask *before*
; `flattenTaintedBranchesInRegion` ever gets a chance to notice (now that
; masking has narrowed block `3`'s `load`) that block `3`'s branch is, in
; fact, mask-dependent, and flatten it into its own, properly-narrower
; per-arm `sideeffect.t`/`sideeffect.f` masks. Those freshly computed
; per-arm masks are strictly more correct than the coarse loop mask, but
; by the time `DiamondFlattener::flattenKnownDivergentBranch` computes
; them, block `7`'s `store` has already been converted into its final,
; masked form -- `applyStageMasks`'s ordinary `StoreInst` handling only
; ever runs once per instruction, and `narrowCallMask`'s own "only if
; still constant" guard (needed everywhere else to protect an
; already-correctly-narrowed mask) refuses to re-narrow it a second time.
; The result: block `7`'s store runs unconditionally for every lane
; still live in the loop, not just the lanes that actually satisfied
; block `3`'s `fcmp` -- confirmed via a real `EQ_BUFFER` byte-for-byte
; diff against the amber test's own reference image. Fixed by having
; `flattenKnownDivergentBranch` re-narrow any stage op the blanket pass
; already masked-and-erased inside its two arms, from that one coarse
; mask down to its own freshly computed, properly per-arm one.
;
; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 %active.live,
; CHECK-NEXT: %sideeffect.t = and i1 %active.sideeffect,
; CHECK: call void @feme.cpu.masked.store.f32(float %0, ptr %1, i32 4, i1 %sideeffect.t)
define void @main(float %0, ptr %1) {
Flow11:
  br label %2

2:                                                ; preds = %Flow._crit_edge, %Flow11
  br label %3

.loopexit:                                        ; preds = %Flow2
  ret void

3:                                                ; preds = %2
  %4 = load float, ptr null, align 4
  %5 = fcmp ult float %4, 0.000000e+00
  br i1 %5, label %7, label %.Flow2_crit_edge

.Flow2_crit_edge:                                 ; preds = %3
  br label %Flow2

Flow2:                                            ; preds = %7, %.Flow2_crit_edge
  %6 = phi i1 [ %9, %7 ], [ false, %.Flow2_crit_edge ]
  br i1 %6, label %.loopexit, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow2
  br label %2

7:                                                ; preds = %3
  store float %0, ptr %1, align 4
  %8 = call float @feme.stage.input.load.f32()
  %9 = fcmp uge float %8, 0.000000e+00
  br label %Flow2
}

declare float @feme.stage.input.load.f32()
