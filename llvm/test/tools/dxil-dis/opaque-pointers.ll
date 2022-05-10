; RUN: llc --filetype=obj %s -o - | dxil-dis -o - | FileCheck %s
target triple = "dxil-unknown-unknown"

define i64 @test(ptr %p) {
  store i32 0, ptr %p
  %v = load i64, ptr %p
  ret i64 %v
}

; CHECK: define i64 @test(i8* %p) {
; CHECK-NEXT: %1 = bitcast i8* %p to i32*
; CHECK-NEXT: store i32 0, i32* %1, align 4
; CHECK-NEXT: %2 = bitcast i8* %p to i64*
; CHECK-NEXT: %3 = load i64, i64* %2, align 8

define i64 @test2(ptr %p) {
  store i64 0, ptr %p
  %v = load i64, ptr %p
  ret i64 %v
}

; CHECK: define i64 @test2(i64* %p) {
; CHECK-NEXT: store i64 0, i64* %p, align 8
; CHECK-NEXT: %v = load i64, i64* %p, align 8
