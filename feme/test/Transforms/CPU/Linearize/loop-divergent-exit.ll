; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; A loop whose only exit is a divergent `break` (see
; `feme/test/Transforms/CPU/CFG/infinite-loop-divergent-exit.ll`, the same
; named shape from the CFG restructurization corpus): the header gains a
; loop-carried "active" mask, the break's target becomes the loop body
; unconditionally (deactivating rather than really exiting), and the
; backedge is taken as long as `feme.cpu.mask.any` says some lane still is.
; See "Loops with divergent exits" in "Phase 3: Linearization and
; Predication" in feme/docs/FeMeCPUDesign.md. `feme::cpu::PreparePass` runs
; first here (unlike the diamond tests) because this shape survives its
; `BreakCriticalEdges` step unchanged -- see the Status section's milestone
; 6 deviation note for the (more common) loop shape that does not yet.

; CHECK-LABEL: define void @main(
; CHECK: %active = phi i1 [ true, %entry ], [ %active.header, %[[LATCH:.*]] ]
; CHECK: %break.cond = icmp eq i32 %tid, %inc
; CHECK: %active.header = and i1 %active, %{{.*}}
; CHECK: br label %[[LATCH]]
; CHECK: [[LATCH]]:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header)
; CHECK: br i1 %loop.any.active, label %loop, label %exit
define void @main() #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %loop]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %inc = add i32 %i, 1
  %break.cond = icmp eq i32 %tid, %inc
  br i1 %break.cond, label %exit, label %loop
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
