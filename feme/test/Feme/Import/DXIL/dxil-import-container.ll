; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme-translate --import-dxil %t.dxcontainer | FileCheck %s

; Round-trips a minimal module through feme::DXILImporter's DXContainer
; path: `llc` (targeting `dxil-...`) emits this file's textual IR as a real
; `DXContainer` object with an embedded DXIL bitcode part -- the same
; encoding a real DXIL shader binary uses (see feme/docs/Design.md's DXIL
; section) -- and feme-translate's `--import-dxil` (feme::DXILImporter)
; unwraps the container and parses the embedded bitcode back. No binary
; fixture is checked in, per "Avoiding binary test fixtures" in
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
