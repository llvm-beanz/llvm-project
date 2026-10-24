; RUN: dxil-as %s -o %t.bc
; RUN: %python %S/Inputs/wrap-dxil.py %t.bc %t.dxil
; RUN: dxil-dis %t.dxil -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: dxil-as %t.ll -o %t.roundtrip.bc
; RUN: cmp %t.bc %t.roundtrip.bc

; CHECK: define void @main()
; CHECK: ret void
; CHECK: !dx.version = !{![[VERSION:[0-9]+]]}
; CHECK: ![[VERSION]] = !{i32 1, i32 0}

target triple = "dxil-pc-shadermodel6.0-compute"

define void @main() {
  ret void
}

!dx.version = !{!0}
!0 = !{i32 1, i32 0}
