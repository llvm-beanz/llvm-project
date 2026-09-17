; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H171: a `dFdx`/`dFdy`/`fwidth`-family derivative (or a
; `subgroupQuadSwap*`/`subgroupQuadBroadcast`-style `QuadRead`) applied to a
; whole GLSL/HLSL vector -- e.g. `dFdx(vec2 coord)` -- decomposes into `N`
; independent per-component wide stage-op calls, exactly like ordinary
; elementwise arithmetic over a divergent vector, rather than building an
; illegal `<W x <N x T>>` nested-vector result the naive
; `FixedVectorType::get(CI.getType(), WaveSize)` widening used to produce
; (the bug this test reproduces -- found reducing a real
; `dEQP-VK.glsl.derivate.dfdx.private_store.vec2_highp` failure down to its
; exact IR shape). Each component's own derivative is fully independent of
; its siblings, so this decomposes exactly like ordinary elementwise
; arithmetic (`widenVectorElementwise`), one `feme.stage.derivative.x.fine.*`
; call per component of the divergent `<2 x float>` operand.

; CHECK-LABEL: define void @main(
; CHECK: call <4 x float> @feme.stage.derivative.x.fine.v4f32(<4 x float>
; CHECK: call <4 x float> @feme.stage.derivative.x.fine.v4f32(<4 x float>
; CHECK-NOT: <4 x <2 x float>>

define void @main() #0 {
  %x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %v0 = insertelement <2 x float> poison, float %x, i64 0
  %y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %v1 = insertelement <2 x float> %v0, float %y, i64 1
  %dx = call <2 x float> @feme.stage.derivative.x.fine.v2f32(<2 x float> %v1)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare <2 x float> @feme.stage.derivative.x.fine.v2f32(<2 x float>)

attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
