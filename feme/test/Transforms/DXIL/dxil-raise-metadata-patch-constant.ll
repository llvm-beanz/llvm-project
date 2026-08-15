; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata -S %s | FileCheck %s

; Companion to dxil-raise-metadata-signature.ll: checks that a hull shader's
; patch-constant signature rows are preserved too (roadmap R18). Direction
; assignment (`PatchOutput` for a hull shader, `PatchInput` for a domain
; shader) is exercised precisely by
; unittests/Transforms/DXIL/SignatureImportTest.cpp's
; `PatchConstantDirectionDependsOnStage`; this only checks the pass attaches
; something for the stage that matters end-to-end.

target triple = "dxil-ms-dx"

; CHECK: target triple = "dxil-unknown-shadermodel6.0-hull"
; CHECK: define void @main() {{.*}} !feme.signature ![[SIG:[0-9]+]]
define void @main() {
  ret void
}
; CHECK-DAG: ![[SIG]] = !{[{{[0-9]+}} x i8] c"{{.*}}"}

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"hs", i32 6, i32 0}
!1 = !{ptr @main, !"main", !2, null, null}

; Signatures tuple: {Input, Output, PatchConstant}. Only a patch-constant
; output row (SV_TessFactor, register 0), matching a hull shader's own
; `!dx.entryPoints` entry (the tessellation factors it produces).
!2 = !{null, null, !3}
!3 = !{!4}
!4 = !{i32 0, !"SV_TessFactor", i8 9, i8 25, !5, i8 0, i32 1, i8 1, i32 0, i8 0, null}
!5 = !{i32 0}
