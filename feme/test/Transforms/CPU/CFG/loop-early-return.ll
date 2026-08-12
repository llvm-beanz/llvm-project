; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A loop whose body can `return` directly, alongside its own normal
; continuation past the loop -- two distinct exits from the function, one
; of them straight out of the loop's body rather than through its latch.
; See "CFG restructurization test suite" in feme/docs/FeMeCPUDesign.md.

define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %loop.cond = icmp slt i32 %i, %n
  br i1 %loop.cond, label %body, label %after
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %return.cond = icmp eq i32 %tid, %i
  br i1 %return.cond, label %early.return, label %latch
early.return:
  ret void
latch:
  %inc = add i32 %i, 1
  br label %loop
after:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
