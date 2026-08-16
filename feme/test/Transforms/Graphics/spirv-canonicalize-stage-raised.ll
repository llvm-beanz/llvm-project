; RUN: feme-opt --llvm -passes=feme-graphics-canonicalize-stage -S %s | FileCheck %s

; feme::graphics::CanonicalizeStagePass renames the `llvm.spv.*` intrinsic
; calls MLIR's SPIR-V -> LLVM conversion already legalizes stage-IO-adjacent
; operations to, into their `feme.stage.*` peers, mirroring
; dxil-canonicalize-stage-raised.ll's DXIL-derived forms (roadmap R20).
; `llvm.spv.discard` (SPIR-V's unconditional `OpKill`) becomes a
; constant-true `feme.stage.discard`, since that op family always takes a
; condition operand.

target triple = "spirv-unknown-vulkan1.3-pixel"

; CHECK-LABEL: define void @main()
define void @main() #0 {
  ; CHECK: call void @feme.stage.discard(i1 true)
  call void @llvm.spv.discard()

  %v = fadd float 1.0, 2.0

  ; CHECK: call float @feme.stage.derivative.x.fine.f32(float %v)
  %ddx = call float @llvm.spv.ddx.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.y.fine.f32(float %v)
  %ddy = call float @llvm.spv.ddy.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.x.coarse.f32(float %v)
  %ddx.coarse = call float @llvm.spv.ddx.coarse.f32(float %v)
  ; CHECK: call float @feme.stage.derivative.y.coarse.f32(float %v)
  %ddy.coarse = call float @llvm.spv.ddy.coarse.f32(float %v)

  ; CHECK: call float @feme.stage.quad.read.f32(float %v, i8 0)
  %quad.x = call float @llvm.spv.quad.read.across.x.f32(float %v)
  ; CHECK: call float @feme.stage.quad.read.f32(float %v, i8 2)
  %quad.diag = call float @llvm.spv.quad.read.across.diagonal.f32(float %v)

  ret void
}

declare void @llvm.spv.discard()
declare float @llvm.spv.ddx.f32(float)
declare float @llvm.spv.ddy.f32(float)
declare float @llvm.spv.ddx.coarse.f32(float)
declare float @llvm.spv.ddy.coarse.f32(float)
declare float @llvm.spv.quad.read.across.x.f32(float)
declare float @llvm.spv.quad.read.across.diagonal.f32(float)

attributes #0 = { "feme.shader.stage"="fragment" }
