; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A loop whose divergent exit check has already gone through
; `feme::cpu::LinearizePass` (see `feme/test/Transforms/CPU/Linearize/
; loop-break.ll`, run here through the actual `feme-cpu-linearize` pass
; instead of hand-written to keep this test honest about what the two
; passes together produce) widens: the loop-carried "active" mask becomes a
; `<4 x i1>` `phi`, and `feme.cpu.mask.any` lowers to the real cross-lane
; reduction, `llvm.vector.reduce.or`, over it (see "Phase 4: Widening" and
; "Mask representation between phases" in feme/docs/FeMeCPUDesign.md).

; CHECK-LABEL: define void @main(
; CHECK: loop:
; CHECK: %i = phi i32 [ 0, %entry ], [ %inc, %latch ]
; CHECK: %active.live.wide = phi <4 x i1> [ splat (i1 true), %entry ], [ %active.header.live.wide, %latch ]
; CHECK: %tid{{.*}} = call <4 x i32> @feme.cpu.builtin.thread_id.v4(
; CHECK: %break.cond.wide = icmp eq <4 x i32> %tid{{.*}}, %i.splat.splat
; CHECK: %active.header.live.wide = and <4 x i1> %active.live.wide, %{{.*}}
; CHECK-NEXT: %active.header.sideeffect.wide = and <4 x i1> %active.sideeffect.wide, %{{.*}}
; CHECK-NEXT: br label %latch
; CHECK: latch:
; CHECK: %loop.cond = icmp slt i32 %inc, %n
; CHECK: %loop.any.active = call i1 @llvm.vector.reduce.or.v4i1(<4 x i1> %active.header.live.wide)
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
