; RUN: llc --filetype=asm %s -o - | FileCheck %s
target triple = "dxil-unknown-unknown"

define i64 @test(ptr %p) {
  store i32 0, ptr %p
  %v = load i64, ptr %p
  ret i64 %v
}

; CHECK: define i64 @test(ptr %p) {
; CHECK-NEXT: %1 = bitcast ptr %p to ptr
; CHECK-NEXT: store i32 0, ptr %1, align 4
; CHECK-NEXT: %2 = bitcast ptr %p to ptr
; CHECK-NEXT: %3 = load i64, ptr %2, align 8
