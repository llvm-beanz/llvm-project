; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; A multi-way `switch`: `LowerSwitch` turns this into a chain of two-way
; branches before `StructurizeCFG` runs, matching "the linearizer handles
; two-way branches only" (see feme::cpu::PreparePass). See "CFG
; restructurization test suite" in feme/docs/FeMeCPUDesign.md.

define void @main(i32 %v) #0 {
entry:
  switch i32 %v, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
  ]
case0:
  br label %end
case1:
  br label %end
case2:
  br label %end
default:
  br label %end
end:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
