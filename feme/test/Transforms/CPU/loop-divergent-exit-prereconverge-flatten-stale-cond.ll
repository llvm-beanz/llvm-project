; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV7(d): reduced (via llvm-reduce) from a real, reproduced
; `dEQP-VK.graphicsfuzz.cov-discard-condition-loop-same-condition-again`
; verifier failure ("Both operands to a binary operator are not of the
; same type! %N = xor ..."), and from the identical symptom seen on
; `cov-fragcoord-clamp-array-access` and
; `stable-colorgrid-modulo-vec3-values-from-matrix`. `linearizeCycle`
; captures `CheckExit->Cond` (here, `Flow5`'s own condition phi) as a raw
; `Value *` snapshot *before* masking `PreRegion` (here, blocks `3` and
; `.Flow5_crit_edge`, the divergent `fcmp`'s own diamond) and calling
; `flattenTaintedBranchesInRegion` on it. That flatten recognizes block
; `3`'s branch as mask-dependent and flattens its whole diamond -- but
; that diamond's own reconvergence point is `CheckBlock` (`Flow5`)
; itself, so `DiamondFlattener` replaces `Flow5`'s condition phi with a
; freshly built `select` via RAUW and erases the original phi outright.
; `CheckExit->Cond`'s raw pointer is left dangling (not a
; `WeakTrackingVH`), so every later read of it (type, bit width, parent)
; observes freed/reused memory -- surfacing many instructions later as a
; bogus-width `xor`/`select`. Fixed by refreshing `CheckExit->Cond` from
; `CheckExit->Br->getCondition()` (which is never itself replaced, so any
; RAUW of its condition operand is reflected automatically) immediately
; after `flattenTaintedBranchesInRegion` runs on `PreRegion`.
;
; CHECK-LABEL: define void @main(
; CHECK: Flow5:
; CHECK: %.linearized = select i1
; CHECK-NEXT: %[[COND:[0-9]+]] = xor i1 %.linearized, true
; CHECK-NEXT: %exited.via.check.this.iter = and i1 %active.live, %[[COND]]
define void @main(i1 %.not) {
._crit_edge:
  br label %0

0:
  br i1 %.not, label %3, label %.Flow4_crit_edge

.Flow4_crit_edge:
  br label %Flow4

Flow5:
  %1 = phi i1 [ %9, %7 ], [ true, %.Flow5_crit_edge ]
  br i1 %1, label %2, label %Flow6

2:
  br label %Flow6

3:
  %4 = call float @feme.cpu.resource.load.raw.f32(ptr null, i32 0, i32 0, i64 0, i1 false)
  %5 = fcmp ogt float %4, 0.000000e+00
  br i1 %5, label %7, label %.Flow5_crit_edge

.Flow5_crit_edge:
  br label %Flow5

Flow4:
  %6 = phi i1 [ %10, %Flow6 ], [ true, %.Flow4_crit_edge ]
  br i1 %6, label %loop.exit.guard, label %Flow4._crit_edge

Flow4._crit_edge:
  br label %0

7:
  %8 = call float @feme.stage.input.load.f32()
  %9 = fcmp uge float %8, 0.000000e+00
  br label %Flow5

Flow6:
  %10 = phi i1 [ false, %2 ], [ true, %Flow5 ]
  br label %Flow4

loop.exit.guard:
  ret void
}

declare float @feme.stage.input.load.f32()
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
