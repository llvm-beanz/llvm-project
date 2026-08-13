; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A fully uniform counted loop is left completely alone: this pass only
; linearizes a loop that actually has a divergent exit check (see "Loops
; with divergent exits" in "Phase 3: Linearization and Predication" in
; feme/docs/FeMeCPUDesign.md); nothing here needs an "active" mask.

; CHECK-LABEL: define void @main(
; CHECK-NOT: %active
; CHECK-NOT: feme.cpu.mask.any
; CHECK: br i1 %loop.cond, label %loop, label %exit
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %loop]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
