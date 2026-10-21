; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A loop with a divergent conditional `continue` (see
; `feme/test/Transforms/CPU/CFG/loop-continue.ll`, the same named shape from
; the CFG restructurization corpus): the internal diamond reconverging back
; at the latch has `body`'s own "take the branch" arm (`latch`) be the
; diamond's own reconvergence point itself, with no block of its own in
; between -- an "empty diamond arm". Previously diagnosed as unsupported;
; `DiamondFlattener::flatten()`'s own `Cur == End` early return already
; handles this shape correctly, so `DiamondFlattener::validate()` no longer
; needs to reject it (roadmap ENV6(a) found and fixed this triaging the
; shared root cause of the 210-case dEQP-VK.api.copy_and_blit.* CTS
; cluster).

; CHECK: define void @main
; CHECK-NOT: feme-cpu-linearize
; CHECK: %live.merge = select i1 %continue.cond, i1 %live.t, i1 %live.f
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
