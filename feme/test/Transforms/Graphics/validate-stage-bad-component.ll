; RUN: not feme-opt --llvm -passes=feme-dxil-raise-metadata,feme-graphics-validate-stage -S %s 2>&1 | FileCheck %s

; feme::graphics::ValidateStagePass diagnoses a `feme.stage.input.load`
; whose constant component operand is out of range for its element's
; declared `[FirstComponent, FirstComponent + ComponentCount)` (roadmap
; R20). Element 0 (POSITION) below is a `float4` (`ComponentCount` 4), so
; component 7 is out of range.

target triple = "dxil-ms-dx"

; CHECK: error: feme-graphics-validate-stage: 'feme.stage.input.load' in function 'main' component 7 is out of range for element 0
define void @main() #0 {
  %v = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 7, i32 0)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

attributes #0 = { "feme.shader.stage"="vertex" }

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"vs", i32 6, i32 0}
!1 = !{ptr @main, !"main", !2, null, null}

!2 = !{!3, null, null}
!3 = !{!4}
!4 = !{i32 0, !"POSITION", i8 9, i8 0, !5, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!5 = !{i32 0}
