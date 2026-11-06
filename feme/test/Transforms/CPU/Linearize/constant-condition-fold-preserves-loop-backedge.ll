; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Regression test for ENV6(c)(5): `foldConstantConditionBranches`'s own
; multi-round instruction-simplification/branch-folding fixed point (added
; to resolve `constant-condition-computed-branch-unreachable-arm.ll`'s own
; non-loop, phi-of-constants shape) must not also re-fold a *genuine* loop's
; own backedge condition once this pass's own `EliminateUnreachableBlocks`
; call happens to collapse that loop header's multi-incoming phi (`%4`
; below) down to its one surviving, constant incoming value (`true`, once
; the dead `br i1 false` arm above it is pruned) -- that reduction is
; exactly the uniform-but-data-dependent-looking input `LoopLinearizer`
; itself expects and lowers via `feme.cpu.mask.any`, not a case this early
; cleanup pass should ever erase the loop's own CFG backedge for. (This
; function has no bare `unreachable` arm at all, so it previously relied on
; the old, single-pass `foldConstantConditionBranches` never re-examining
; the loop header's own phi-turned-literal condition a second time; see
; `LinearizeTest.TracksUniformityOfOwnFlattenedDiamondMergeAcrossLoopExit`
; for the same shape at the unit-test level.)
;
; CHECK-LABEL: define void @main(
; CHECK: call i1 @feme.cpu.mask.any
define void @main() {
  br label %loop.header

loop.header:
  br i1 false, label %dead, label %loop.header.live

dead:
  %v = call float @feme.stage.input.load.f32()
  %cmp = fcmp ult float 0.000000e+00, %v
  br label %loop.latch2

loop.header.live:
  br label %loop.latch

loop.latch:
  %cond = phi i1 [ %cmp, %loop.latch2 ], [ true, %loop.header.live ]
  br i1 %cond, label %loop.exit, label %loop.backedge

loop.backedge:
  br label %loop.header

loop.latch2:
  br label %loop.latch

loop.exit:
  ret void
}
declare float @feme.stage.input.load.f32()
