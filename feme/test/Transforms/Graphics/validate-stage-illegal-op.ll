; RUN: not feme-opt --llvm -passes=feme-graphics-validate-stage -S %s 2>&1 | FileCheck %s

; feme::graphics::ValidateStagePass diagnoses a `feme.stage.*` operation
; that is not legal for its entry point's declared stage: `discard` is
; fragment-only (roadmap R20; see "Canonical stage operations" in
; feme/docs/FeMeGraphicsDesign.md), so using it in a vertex entry point is
; malformed input.

target triple = "dxil-unknown-shadermodel6.0-vertex"

; CHECK: error: feme-graphics-validate-stage: 'feme.stage.discard' is not legal in function 'main' (stage 'vertex')
define void @main() #0 {
  call void @feme.stage.discard(i1 true)
  ret void
}

declare void @feme.stage.discard(i1)

attributes #0 = { "feme.shader.stage"="vertex" }
