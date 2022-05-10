; RUN: llc %s --filetype=asm -o - | FileCheck %s
target triple = "dxil-unknown-unknown"

define i32 @add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}

; CHECK: @0 = private constant [[BC_TYPE:\[[0-9]+ x i8\]]] c"BC\C0\DE{{[^"]+}}", section "DXIL", align 4
; CHECK: @llvm.compiler.used = appending global [1 x i8*] [i8* getelementptr
; inbounds ([[BC_TYPE]], [[BC_TYPE]]* @0, i32 0, i32 0)], section
; "llvm.metadata"
