; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (Roadmap H2d) The shape a real SPIR-V-derived `gl_PerVertex` access
; actually takes (confirmed against a real `dEQP-VK.multiview` vertex
; shader): each member -- even each individual component of `gl_Position`
; -- is addressed by its own scalar load/store, either a bare
; `@gl_PerVertex` global (member 0, component 0 -- SPIR-V's own offset-0
; access, which LLVM's constant-`getelementptr` folding erases entirely) or
; a `getelementptr (i8, ptr @gl_PerVertex, i64 ByteOffset)` `ConstantExpr`
; (every other member/component), never the whole-struct aggregate shape
; spirv-canonicalize-stage-interface-block.ll exercises.
; `feme::graphics::CanonicalizeStagePass`'s `resolveStageIOAccess`/
; `getStageIOBaseAndOffset` resolve each of these back to its own
; `ElementID` and `(Row, Component)` pair via the block's own
; `StructLayout` (`{<4 x float>, float, [1 x float], [1 x float]}`:
; `Position` at byte 0, `PointSize` at 16, `ClipDistance` at 20).

target triple = "spirv-unknown-vulkan1.3-vertex"

@gl_PerVertex = external addrspace(8) global { <4 x float>, float, [1 x float], [1 x float] }, !feme.spirv.MemberDecorations !10

; CHECK-LABEL: define void @main()
define void @main() #0 {
  ; `gl_Position.x` (member 0, component 0): a bare global store.
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float 1.000000e+00, i32 0)
  store float 1.0, ptr addrspace(8) @gl_PerVertex

  ; `gl_Position.y` (member 0, component 1, byte offset 4): negated
  ; (roadmap H2g, `negateSystemValuePositionY`) to normalize SPIR-V's
  ; Y-down clip space into the Y-up convention the executor's own
  ; viewport transform assumes (matching DXIL's `SV_Position`, the other
  ; producer of a `SignatureSystemValue::Position` output).
  ; CHECK: call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float -2.000000e+00, i32 0)
  store float 2.0, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 4)

  ; `gl_PointSize` (member 1, whole value, byte offset 16).
  ; CHECK: call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float 3.000000e+00, i32 0)
  store float 3.0, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 16)

  ; `gl_ClipDistance[0]` (member 2, row 0, byte offset 20).
  ; CHECK: call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 4.000000e+00, i32 0)
  store float 4.0, ptr addrspace(8) getelementptr inbounds nuw (i8, ptr addrspace(8) @gl_PerVertex, i64 20)

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
