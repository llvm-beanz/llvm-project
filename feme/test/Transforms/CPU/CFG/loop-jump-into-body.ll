; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A branch straight from outside the loop into the middle of its body
; (skipping the header): the loop's true header no longer dominates every
; block inside it, another irreducible shape `FixIrreducible` must resolve.
; See "CFG restructurization test suite" in feme/docs/FeMeCPUDesign.md.

define void @main(i32 %n, i1 %skip.header) #0 {
entry:
  br i1 %skip.header, label %body, label %header
header:
  br label %body
body:
  %i = phi i32 [0, %header], [%inc, %body]
  %inc = add i32 %i, 1
  %cond = icmp slt i32 %inc, %n
  br i1 %cond, label %body, label %exit
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
