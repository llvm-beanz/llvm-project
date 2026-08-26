; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap H2d) A builtin interface block (glslang's implicit `gl_PerVertex`,
; `{Position, PointSize, ClipDistance[1], CullDistance[1]}`) carries no
; whole-variable `!spirv.Decorations` -- SPIR-V decorates its members
; individually -- but a per-member `!feme.spirv.MemberDecorations` one
; instead (roadmap H2c; see spirv-to-llvmir-stage-io-member-decorations.mlir
; for the SPIR-V import side that produces this shape).
; `feme::graphics::CanonicalizeStagePass` now decomposes it into one
; `SignatureElement`/`ElementID` per member, routing each member's own store
; through its own `ElementID` rather than the whole block's single one.

target triple = "spirv-unknown-vulkan1.3-vertex"

@gl_PerVertex = external addrspace(8) global { <4 x float>, float, [1 x float], [1 x float] }, !feme.spirv.MemberDecorations !10

; CHECK-LABEL: define void @main()
define void @main() #0 {
  %agg0 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } poison, <4 x float> <float 0.0, float 0.0, float 0.0, float 1.0>, 0
  %agg1 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } %agg0, float 1.0, 1
  %agg2 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } %agg1, [1 x float] [float 0.0], 2
  %agg3 = insertvalue { <4 x float>, float, [1 x float], [1 x float] } %agg2, [1 x float] [float 0.0], 3

  ; `gl_Position` (member 0, ElementID 0) is stored one component at a
  ; time, matching every other vector-typed stage-IO output.
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float {{.*}}, i32 0)
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float {{.*}}, i32 0)

  ; `gl_PointSize` (member 1, ElementID 1) is a scalar: one store.
  ; CHECK: call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float {{.*}}, i32 0)

  ; `gl_ClipDistance`/`gl_CullDistance` (members 2/3, ElementIDs 2/3) are
  ; each a 1-element array: one store per element, routed through their
  ; own `ElementID`.
  ; CHECK: call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float {{.*}}, i32 0)
  ; CHECK: call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float {{.*}}, i32 0)
  store { <4 x float>, float, [1 x float], [1 x float] } %agg3, ptr addrspace(8) @gl_PerVertex

  ret void
}

attributes #0 = { "feme.shader.stage"="vertex" }

!10 = !{!11, !12, !13, !14}
!11 = !{i32 0, !15} ; member 0: gl_Position
!12 = !{i32 1, !16} ; member 1: gl_PointSize
!13 = !{i32 2, !17} ; member 2: gl_ClipDistance
!14 = !{i32 3, !18} ; member 3: gl_CullDistance
!15 = !{!19}
!19 = !{i32 11, i32 0} ; BuiltIn Position
!16 = !{!20}
!20 = !{i32 11, i32 1} ; BuiltIn PointSize
!17 = !{!21}
!21 = !{i32 11, i32 3} ; BuiltIn ClipDistance
!18 = !{!22}
!22 = !{i32 11, i32 4} ; BuiltIn CullDistance
