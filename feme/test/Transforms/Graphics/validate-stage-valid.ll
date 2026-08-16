; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata,feme-graphics-canonicalize-stage,feme-graphics-validate-stage -S %s | FileCheck %s

; feme::graphics::ValidateStagePass diagnoses nothing when a fragment entry
; point's canonical `feme.stage.*` calls all have in-range constant
; element/row/component operands and are legal for the declared stage
; (roadmap R20). Running the whole pipeline -- metadata raising, then
; canonicalization, then validation -- exercises this the way it would run
; for real, rather than hand-assembling `feme.stage.*` calls directly.

target triple = "dxil-ms-dx"

; CHECK-LABEL: define void @main()
define void @main() {
  %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %v)
  ret void
}

declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float)

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"ps", i32 6, i32 0}
!1 = !{ptr @main, !"main", !2, null, null}

!2 = !{!3, !5, null}
!3 = !{!4}
!4 = !{i32 0, !"POSITION", i8 9, i8 0, !6, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!5 = !{!7}
!6 = !{i32 0}
!7 = !{i32 0, !"SV_Position", i8 9, i8 3, !6, i8 0, i32 1, i8 4, i32 0, i8 0, null}
