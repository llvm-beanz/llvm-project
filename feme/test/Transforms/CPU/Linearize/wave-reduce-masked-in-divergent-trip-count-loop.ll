; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L298: a convergent wave intrinsic call (`WaveActiveBitAnd`'s own
; `llvm.spv.wave.reduce.and.i32`, here) sitting directly in the body of a
; loop whose own header check is itself the loop's *only* divergent
; decision (a per-lane-varying trip count, with no separate `CheckBlock`
; elsewhere in the cycle -- `DivergentCandidates` is empty) used to reach
; its lowered intrinsic call with **no masking select inserted at all**,
; ANDing in every lane's current value unconditionally regardless of
; whether that lane had already finished iterating. `linearizeCycle`'s
; `HeaderDivergent`-only fallback path (reached whenever
; `DivergentCandidates` is empty, i.e. no separate `CheckBlock`-style
; shape) called `applyStageMasks` -- the pass that inserts this masking
; select -- on `Header`/`Latch`/`ExitBlock` only, never on the loop's own
; body blocks in between: a stale comment at this exact call site claimed
; "this shape would have been rejected already" for any such mask-
; affecting call in the body, but nothing in this function actually
; checked for one. Found reducing `WaveOps/WaveActiveBitAnd.convergence.
; test`'s own divergent "each thread iterates TID.x times" sub-case
; (`Out5`) down to this exact shape: expected per-lane output
; `[0xFFF0, 0xF, 0xF, 0xF]`, FeMe produced `[0xFFF0, 0, 0, 0]` (every
; inactive lane's own stale, unmasked value got ANDed in, zeroing every
; nibble the four input values didn't all independently agree on).
;
; Fixed by replacing that generic `rethreadNestedEntryMasks`-only loop
; with a `collectUniformPassThroughRegion`-validated walk of the body
; region (mirroring the `DivergentCandidates`-non-empty branch's own
; `PreRegion`/`PostRegion` handling just above it), calling
; `applyStageMasks` on every block in it -- this also turns the stale
; "would have been rejected already" assumption into a real, enforced
; check: a body with a genuinely unsupported second divergent branch now
; correctly diagnoses and bails instead of silently miscompiling.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %active.header.live = and i1 %active.live, %cmp
; CHECK: body:
; CHECK: %wave.reduce.masked = select i1 %active.header.live, i32 %r, i32 -1
; CHECK: call i32 @llvm.spv.wave.reduce.and.i32(i32 %wave.reduce.masked)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %v = call i32 @llvm.dx.resource.load(i32 %tid)
  br label %header

header:
  %r = phi i32 [ %v, %entry ], [ %r.next, %flow.crit_edge ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %flow.crit_edge ]
  %cmp = icmp ult i32 %i, %tid
  br i1 %cmp, label %body, label %flow.crit_edge2

flow.crit_edge2:
  br label %flow

body:
  %r2 = call i32 @llvm.spv.wave.reduce.and.i32(i32 %r)
  %i.next = add i32 %i, 1
  br label %flow

flow:
  %r.next = phi i32 [ %r2, %body ], [ poison, %flow.crit_edge2 ]
  %i.next2 = phi i32 [ %i.next, %body ], [ poison, %flow.crit_edge2 ]
  %done = phi i1 [ false, %body ], [ true, %flow.crit_edge2 ]
  br i1 %done, label %exit, label %flow.crit_edge

flow.crit_edge:
  br label %header

exit:
  call void @llvm.dx.resource.store(i32 %tid, i32 %r)
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.resource.load(i32)
declare void @llvm.dx.resource.store(i32, i32)
declare i32 @llvm.spv.wave.reduce.and.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
