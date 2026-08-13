; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A loop with a divergent break check in its header and a separate latch
; with its own (uniform) natural continuation condition -- the same shape as
; `feme/test/Transforms/CPU/CFG/loop-break.ll`, but run directly against
; `feme-cpu-linearize` on already-structured IR rather than through
; `feme-cpu-prepare`: `StructurizeCFG`'s general "Flow" merge-block scheme
; restructures this particular shape into an internal-diamond-inside-a-loop
; form this milestone does not yet linearize (see the Status section's
; milestone 6 deviation note in feme/docs/FeMeCPUDesign.md).
;
; The header's divergent check becomes an unconditional continuation to the
; latch that updates the "active" mask instead of really exiting; the
; latch's own uniform condition is conjoined with `feme.cpu.mask.any` of
; that mask, so the loop keeps iterating until every lane is either done or
; deactivated. See "Loops with divergent exits" in "Phase 3: Linearization
; and Predication" in feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: loop:
; CHECK: %active = phi i1 [ true, %entry ], [ %active.header, %latch ]
; CHECK: %break.cond = icmp eq i32 %tid, %i
; CHECK: %active.header = and i1 %active, %{{.*}}
; CHECK-NEXT: br label %latch
; CHECK: latch:
; CHECK: %loop.cond = icmp slt i32 %inc, %n
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header)
; CHECK: %loop.continue = and i1 %loop.cond, %loop.any.active
; CHECK: br i1 %loop.continue, label %loop, label %exit
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
