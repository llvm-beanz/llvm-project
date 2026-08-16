; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,feme-graphics-canonicalize-stage -S %s | FileCheck %s

; feme::graphics::CanonicalizeStagePass renames the `llvm.dx.*` intrinsic
; calls feme::dxil::OpRaisingPass already raises `Discard`(82)/
; `DerivCoarseX`/`DerivCoarseY`/`DerivFineX`/`DerivFineY`(83-86)/`QuadOp`(123)
; to, into their `feme.stage.*` peers (roadmap R20). This needs no
; `!feme.signature` at all, unlike `loadInput`/`storeOutput` -- see
; dxil-canonicalize-stage.ll for those.

target triple = "dxil-unknown-shadermodel6.6-pixel"

; CHECK-LABEL: define void @main()
define void @main() #0 {
  ; CHECK: call void @feme.stage.discard(i1 true)
  call void @dx.op.discard(i32 82, i1 true)

  %v = fadd float 1.0, 2.0

  ; CHECK: call float @feme.stage.derivative.x.coarse.f32(float %v)
  %ddx.coarse = call float @dx.op.unary.f32(i32 83, float %v)
  ; CHECK: call float @feme.stage.derivative.y.coarse.f32(float %v)
  %ddy.coarse = call float @dx.op.unary.f32(i32 84, float %v)
  ; CHECK: call float @feme.stage.derivative.x.fine.f32(float %v)
  %ddx.fine = call float @dx.op.unary.f32(i32 85, float %v)
  ; CHECK: call float @feme.stage.derivative.y.fine.f32(float %v)
  %ddy.fine = call float @dx.op.unary.f32(i32 86, float %v)

  ; CHECK: call float @feme.stage.quad.read.f32(float %v, i8 0)
  %quad.x = call float @dx.op.quadOp.f32(i32 123, float %v, i8 0)
  ; CHECK: call float @feme.stage.quad.read.f32(float %v, i8 1)
  %quad.y = call float @dx.op.quadOp.f32(i32 123, float %v, i8 1)
  ; CHECK: call float @feme.stage.quad.read.f32(float %v, i8 2)
  %quad.diag = call float @dx.op.quadOp.f32(i32 123, float %v, i8 2)

  ret void
}

declare void @dx.op.discard(i32, i1)
declare float @dx.op.unary.f32(i32, float)
declare float @dx.op.quadOp.f32(i32, float, i8)

attributes #0 = { "hlsl.shader"="pixel" }
