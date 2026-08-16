; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata,feme-graphics-canonicalize-stage -S %s | FileCheck %s

; feme::graphics::CanonicalizeStagePass rewrites DXIL's `loadInput`(4)/
; `storeOutput`(5) opcodes -- which have no LLVM intrinsic form for
; feme::dxil::OpRaisingPass to raise, so this pass raises them directly --
; into the canonical `feme.stage.input.load`/`output.store` calls
; (roadmap R20; see "Canonical stage operations" in
; feme/docs/FeMeGraphicsDesign.md), resolving each call's DXIL per-list
; signature ID through the `!feme.signature` metadata
; feme::dxil::MetadataRaisingPass attaches (roadmap R18). It also raises
; `IsHelperLane`(221) and the pull-model interpolation family
; (`EvalCentroid`(89)/`EvalSampleIndex`(88)/`EvalSnapped`(87)) the same way.
;
; `feme-dxil-raise-metadata` runs first here purely to produce a realistic
; `!feme.signature` the same way the real pipeline would; this pass does
; not otherwise depend on anything else that pass does.

target triple = "dxil-ms-dx"

; CHECK-LABEL: define void @main()
define void @main() {
  ; One input element (POSITION, float4, DXIL signature ID 0) and one
  ; output element (SV_Position, DXIL signature ID 0): both convert to
  ; feme's combined ElementID 0 and 1 respectively (inputs numbered before
  ; outputs; see SignatureImport.cpp).
  ; CHECK: [[V:%.*]] = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %v = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 0)

  ; CHECK: call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float [[V]], i32 0)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %v)

  ; CHECK: call i1 @feme.stage.is_helper()
  %h = call i1 @dx.op.isHelperLane.i1(i32 221)

  ; CHECK: call float @feme.stage.interpolate.at.centroid.f32(i32 0, i32 1)
  %c = call float @dx.op.evalCentroid.f32(i32 89, i32 0, i32 0, i8 1)

  ; CHECK: call float @feme.stage.interpolate.at.sample.f32(i32 0, i32 1, i32 2)
  %s = call float @dx.op.evalSampleIndex.f32(i32 88, i32 0, i32 0, i8 1, i32 2)

  ; CHECK: call float @feme.stage.interpolate.at.offset.f32(i32 0, i32 1, i32 2, i32 3)
  %o = call float @dx.op.evalSnapped.f32(i32 87, i32 0, i32 0, i8 1, i32 2, i32 3)

  ret void
}

declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32)
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float)
declare i1 @dx.op.isHelperLane.i1(i32)
declare float @dx.op.evalCentroid.f32(i32, i32, i32, i8)
declare float @dx.op.evalSampleIndex.f32(i32, i32, i32, i8, i32)
declare float @dx.op.evalSnapped.f32(i32, i32, i32, i8, i32, i32)

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"ps", i32 6, i32 0}
!1 = !{ptr @main, !"main", !2, null, null}

; Signatures tuple: {Input, Output, PatchConstant}.
!2 = !{!3, !5, null}
!3 = !{!4}
!4 = !{i32 0, !"POSITION", i8 9, i8 0, !6, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!5 = !{!7}
!6 = !{i32 0}
!7 = !{i32 0, !"SV_Position", i8 9, i8 3, !6, i8 0, i32 1, i8 4, i32 0, i8 0, null}
