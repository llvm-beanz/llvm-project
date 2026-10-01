; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; A `for` loop with a divergent early `break`, run through the full
; `feme-cpu-prepare` pipeline first (as `feme::Driver`/`feme-run` do) so
; `StructurizeCFG` restructures it the way it restructures every such loop
; in practice (see `feme/test/Transforms/CPU/Linearize/loop-break.ll`'s own
; comment): the break condition becomes the header's own `CondBr`, but its
; two successors (`exit` and the latch) are both reached only through a
; single shared `Flow`-style dispatch block -- the latch's own, separately
; uniform trip-count re-check -- rather than either successor being
; `exit`/the latch directly. `feme::cpu::DiamondFlattener` correctly
; recognizes this shared-dispatch shape as the cycle's own boundary (see
; `isLoopControlEdgeThroughRelay`) and leaves the header's break check
; completely untouched; `feme::cpu::LoopLinearizer` then recovers it via
; `recoverRelayExitCheck`'s structural fallback (Roadmap L292) -- the
; header's own two successors are the latch (a real loop block) and a
; trivial, single-predecessor relay stub that `peelConstantFlowPredecessors
; InCycle` has already redirected straight to `exit` -- and masks the
; header's own check directly, rather than at a separate "check" block.
; See the Status section's milestone 6 deviation note in
; feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.header.live, {{.*}} ]
; CHECK: %active.header.live = and i1 %active.live,
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header.live)
; CHECK: br i1 %loop.any.active, label %loop, label %exit
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %latch
latch:
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
