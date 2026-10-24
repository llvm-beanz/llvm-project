; RUN: dxil-as %s -o %t.bc
; RUN: llvm-bcanalyzer -dump %t.bc | FileCheck %s --check-prefix=BC
; RUN: dxil-dis -preserve-dxil-uselistorder %t.bc -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: dxil-as %t.ll -o %t.roundtrip.bc
; RUN: cmp %t.bc %t.roundtrip.bc

; BC: <USELIST_BLOCK
; BC: <USELIST_CODE_DEFAULT op0=1 op1=0
; CHECK: uselistorder i32* @g, { 1, 0 }
; CHECK: uselistorder i32 %x, { 1, 0 }

@g = global i32 7
@first = global i32* @g
@second = global i32* @g

define i32 @f(i32 %x) {
  %a = add i32 %x, 1
  %b = mul i32 %x, %a
  ret i32 %b
  uselistorder i32 %x, { 1, 0 }
}

uselistorder i32* @g, { 1, 0 }
