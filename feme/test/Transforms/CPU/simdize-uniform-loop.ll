; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A loop with no divergence at all (every value in it is the same for every
; lane) is left as ordinary scalar control flow: roadmap milestone 7 lifts
; the "no loop at all" restriction milestone 4 had, but a uniform loop needs
; no widening in the first place (see "Uniform value" in "Phase 4:
; Widening").

; CHECK-LABEL: define void @main(
; CHECK: loop:
; CHECK-NEXT: %i = phi i32 [ 0, %entry ], [ %next, %loop ]
; CHECK-NEXT: %next = add i32 %i, 1
; CHECK-NEXT: %done = icmp sge i32 %next, %n
; CHECK-NEXT: br i1 %done, label %exit, label %loop
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
