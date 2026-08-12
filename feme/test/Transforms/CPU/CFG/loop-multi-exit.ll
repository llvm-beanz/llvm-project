; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A loop with two distinct exit blocks before Phase 1 runs: `UnifyLoopExits`
; is exactly the pass that funnels both into one, which
; `-verify-structured`'s "unique exit block" check then confirms. See "CFG
; restructurization test suite" in feme/docs/FeMeCPUDesign.md.

define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %c1 = icmp eq i32 %i, 5
  br i1 %c1, label %exit1, label %latch
latch:
  %inc = add i32 %i, 1
  %c2 = icmp sge i32 %inc, %n
  br i1 %c2, label %exit2, label %loop
exit1:
  ret void
exit2:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
