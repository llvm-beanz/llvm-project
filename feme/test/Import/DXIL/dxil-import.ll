; RUN: llvm-as %s -o %t.bc
; RUN: feme-translate --import-dxil %t.bc | FileCheck %s

; Round-trips a minimal module through feme::DXILImporter's raw-bitcode
; path: `llvm-as` assembles this file's textual IR into an LLVM bitcode
; file (no DXContainer wrapper), matching the "an LLVM bitcode file"
; encoding described in feme/docs/Design.md's DXIL section, and
; feme-translate's `--import-dxil` (feme::DXILImporter) parses it back. No
; binary fixture is checked in, per "Avoiding binary test fixtures" in
; feme/docs/Design.md.

target triple = "dxil-unknown-shadermodel6.5-library"

define i32 @add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}

; CHECK: define i32 @add(i32 %a, i32 %b) {
; CHECK-NEXT:   %sum = add i32 %a, %b
; CHECK-NEXT:   ret i32 %sum
; CHECK-NEXT: }
