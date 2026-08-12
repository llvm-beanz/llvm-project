; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A loop is diagnosed rather than mis-widened: roadmap milestone 4 covers
; acyclic, uniform-control-flow shaders only (see "Phase 4: Widening").

; CHECK: error: feme-cpu-simdize: function 'main' has a loop
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %next = add i32 %i, 1
  %done = icmp sge i32 %next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
