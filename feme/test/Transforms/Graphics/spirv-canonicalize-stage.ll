; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; feme::graphics::CanonicalizeStagePass rewrites a non-builtin `Input`/
; `Output` stage-IO variable's load/store (the shape roadmap R19's SPIR-V
; import produces: an ordinary global in address space 7/8, with
; `Location`/`Component`/interpolation decorations preserved as
; `!spirv.Decorations` metadata -- see spirv-to-llvmir-stage-io.mlir) into
; `feme.stage.input.load`/`output.store`, building this entry's
; `feme::EntrySignature` from those decorations along the way -- the piece
; R19 explicitly deferred to this milestone (roadmap R20; see "Signature
; reflection" in feme/docs/FeMeGraphicsDesign.md).

target triple = "spirv-unknown-vulkan1.3-pixel"

; A flat i32 input at location 0, and a float4 output at location 0.
@in_var = external addrspace(7) constant i32, !spirv.Decorations !0
@out_var = external addrspace(8) global <4 x float>, !spirv.Decorations !3

; CHECK-LABEL: define void @main()
define void @main() #0 {
  ; CHECK: [[V:%.*]] = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
  %v = load i32, ptr addrspace(7) @in_var

  %f = sitofp i32 %v to float
  %cst = insertelement <4 x float> poison, float %f, i32 0

  ; A vector-typed interface variable is stored one component at a time:
  ; the `feme.stage.*` family carries a `Component` operand precisely so a
  ; varying is addressed the same scalar way DXIL's own `storeOutput` does,
  ; and `feme::cpu::SIMDizePass` has no widened form for a whole divergent
  ; vector value.
  ; CHECK: [[C0:%.*]] = extractelement <4 x float> {{.*}}, i64 0
  ; CHECK: call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float [[C0]], i32 0)
  ; CHECK: [[C3:%.*]] = extractelement <4 x float> {{.*}}, i64 3
  ; CHECK: call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float [[C3]], i32 0)
  store <4 x float> %cst, ptr addrspace(8) @out_var

  ret void
}

attributes #0 = { "feme.shader.stage"="fragment" }

!0 = !{!1, !2}
!1 = !{i32 30, i32 0} ; Location 0
!2 = !{i32 14}        ; Flat
!3 = !{!1}
