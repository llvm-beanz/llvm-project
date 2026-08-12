; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A loop with no natural back-edge condition at all -- its only exit is a
; divergent `break` inside the body. See "CFG restructurization test suite"
; in feme/docs/FeMeCPUDesign.md.

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
