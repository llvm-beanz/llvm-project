; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; A `for` loop with a divergent early `break`, run through the full
; `feme-cpu-prepare` pipeline first (as `feme::Driver`/`feme-run` do) so
; `StructurizeCFG` restructures it the way it restructures every such loop
; in practice (see `feme/test/Transforms/CPU/Linearize/loop-break.ll`'s own
; comment): the break condition becomes a diamond entirely inside the loop
; body, reconverging at a `Flow` merge block that -- rather than the header
; or the latch directly -- ends up holding the loop's one real exit check.
; `feme::cpu::DiamondFlattener` flattens that inner diamond first (it does
; not cross the loop's own back edge/exit edge, but a plain if/else fully
; inside the loop body is otherwise an ordinary diamond to it, wherever it
; sits); `feme::cpu::LoopLinearizer` then finds the exit check in that
; third block, reached from the header and reaching the latch each via a
; straight, unconditional chain, and linearizes the loop around it. See the
; Status section's milestone 6 deviation note in
; feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.check.live, {{.*}} ]
; CHECK: %active.check.live = and i1 %active.live,
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
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
