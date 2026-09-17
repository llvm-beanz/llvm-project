; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H171: the `QuadRead` counterpart of simdize-vector-derivative.ll --
; a `subgroupQuadSwap*`/`subgroupQuadBroadcast`-style quad read applied to a
; whole vector (e.g. a `vec2`-typed `subgroupQuadSwapHorizontal`) decomposes
; into `N` independent per-component wide calls, each sharing the same
; compile-time-constant `Dir` operand unchanged, exactly like a
; vector-typed derivative call.

; CHECK-LABEL: define void @main(
; CHECK: call <4 x float> @feme.stage.quad.read.v4f32(<4 x float> {{[^,]*}}, <4 x i8> {{.*}})
; CHECK: call <4 x float> @feme.stage.quad.read.v4f32(<4 x float> {{[^,]*}}, <4 x i8> {{.*}})
; CHECK-NOT: <4 x <2 x float>>

define void @main() #0 {
  %x = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %v0 = insertelement <2 x float> poison, float %x, i64 0
  %y = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %v1 = insertelement <2 x float> %v0, float %y, i64 1
  %qr = call <2 x float> @feme.stage.quad.read.v2f32(<2 x float> %v1, i8 1)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare <2 x float> @feme.stage.quad.read.v2f32(<2 x float>, i8)

attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
