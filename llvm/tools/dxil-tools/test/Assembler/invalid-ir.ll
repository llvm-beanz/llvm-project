; RUN: not dxil-as %s -o %t.bc 2>&1 | FileCheck %s
; RUN: dxil-as -disable-verify %s -o %t.bc
; CHECK: assembly parsed, but does not verify as correct!
; CHECK: Instruction does not dominate all uses!

define i32 @broken(i1 %cond, i32 %value) {
entry:
  br i1 %cond, label %left, label %right
left:
  %sum = add i32 %value, 1
  br label %exit
right:
  br label %exit
exit:
  ret i32 %sum
}
