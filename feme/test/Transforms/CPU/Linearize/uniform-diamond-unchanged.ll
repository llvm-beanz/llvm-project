; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A uniform if/else is left completely alone: "Uniform branches stay
; branches" is the entire payoff of Phase 2 (see "Phase 3: Linearization and
; Predication" in feme/docs/FeMeCPUDesign.md).

; CHECK-LABEL: define void @main(
; CHECK: br i1 %c, label %t, label %f
; CHECK: t:
; CHECK: f:
; CHECK: end:
; CHECK: %v = phi i32 [ %a, %t ], [ %b, %f ]
define void @main(i32 %uniform_cond) #0 {
entry:
  %c = icmp sgt i32 %uniform_cond, 0
  br i1 %c, label %t, label %f
t:
  %a = add i32 %uniform_cond, 1
  br label %end
f:
  %b = add i32 %uniform_cond, 2
  br label %end
end:
  %v = phi i32 [%a, %t], [%b, %f]
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
