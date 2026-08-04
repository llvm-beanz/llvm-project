; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: not feme %t.dxcontainer -o %t.out 2>&1 | FileCheck %s

; `feme` requires --target to know what to retarget to; reject cleanly
; rather than crashing when it isn't given.

target triple = "dxil-unknown-shadermodel6.5-library"

define i32 @add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}

; CHECK: feme: --target must name an output format or target triple
