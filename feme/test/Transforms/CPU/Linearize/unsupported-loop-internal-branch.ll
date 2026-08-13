; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; A loop with a divergent conditional `continue` (see
; `feme/test/Transforms/CPU/CFG/loop-continue.ll`, the same named shape from
; the CFG restructurization corpus): the internal diamond reconverging back
; at the latch is a combination this milestone's loop linearizer does not
; yet handle -- it only recognizes a divergent exit check directly in the
; header and/or the latch (see the Status section's milestone 6 deviation
; note in feme/docs/FeMeCPUDesign.md). Diagnosed and left untouched.

; CHECK: feme-cpu-linearize: function 'main': loop at 'loop' has an internal branch in 'body'
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %loop.cond = icmp slt i32 %i, %n
  br i1 %loop.cond, label %body, label %exit
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %continue.cond = icmp eq i32 %tid, %i
  br i1 %continue.cond, label %latch, label %work
work:
  br label %latch
latch:
  %inc = add i32 %i, 1
  br label %loop
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
