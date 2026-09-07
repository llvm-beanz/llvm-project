; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; (roadmap L69) Unlike every other `llvm.spv.*` stage-op intrinsic this
; pass already renames for a fragment entry (see
; spirv-canonicalize-stage-raised.ll's sibling test), a *compute* entry's
; own `llvm.spv.ddx`/`.ddy`/`.coarse` calls -- the raised form of
; `OpDPdx`/`OpDPdy`, which `VK_KHR_compute_shader_derivatives` lets a
; compute shader use when it declares a `DerivativeGroupQuadsKHR`/
; `DerivativeGroupLinearKHR` execution mode -- used to be left completely
; untouched: `CanonicalizeStagePass::run` only ever dispatched to
; `canonicalizeSPIRVStage` for a fixed list of graphics stages that did not
; include `ShaderStage::Compute`, so nothing later in this CPU target's own
; pipeline (which only recognizes the canonical `feme.stage.derivative.*`
; call this pass produces, never a raw `llvm.spv.ddx`/`.ddy`) could lower
; it, and `vkCreateComputePipelines` failed outright on any such shader.
; This test confirms a compute entry now gets the exact same rewrite a
; fragment entry already does.

target triple = "spirv-unknown-vulkan1.3-pixel"

; CHECK-LABEL: define void @main()
define void @main() #0 {
  %v = fadd float 1.0, 2.0

  ; CHECK: call float @feme.stage.derivative.x.fine.f32(float %v)
  %ddx = call float @llvm.spv.ddx.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.y.fine.f32(float %v)
  %ddy = call float @llvm.spv.ddy.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.x.coarse.f32(float %v)
  %ddx.coarse = call float @llvm.spv.ddx.coarse.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.y.coarse.f32(float %v)
  %ddy.coarse = call float @llvm.spv.ddy.coarse.f32(float %v)

  ret void
}

declare float @llvm.spv.ddx.f32(float)
declare float @llvm.spv.ddy.f32(float)
declare float @llvm.spv.ddx.coarse.f32(float)
declare float @llvm.spv.ddy.coarse.f32(float)

attributes #0 = { "feme.shader.stage"="compute" }
