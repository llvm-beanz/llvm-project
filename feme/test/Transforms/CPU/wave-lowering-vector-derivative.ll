; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H171: the end-to-end counterpart of simdize-vector-derivative.ll --
; confirms a vector-typed `dFdx` decomposes into independent per-component
; wide calls that `feme::cpu::WaveLoweringPass` then lowers into two
; independent quad-shuffle sequences, one per component, with no
; `feme.stage.derivative.*` declaration left behind (mirroring
; wave-lowering-derivative-quad.ll's own scalar case).

; CHECK-LABEL: define void @main(
; CHECK: %quad.left = shufflevector <4 x float>
; CHECK: %quad.right = shufflevector <4 x float>
; CHECK: fsub <4 x float> %quad.right, %quad.left
; CHECK: %quad.left{{[0-9]*}} = shufflevector <4 x float>
; CHECK: %quad.right{{[0-9]*}} = shufflevector <4 x float>
; CHECK: fsub <4 x float>
; CHECK-NOT: feme.stage.derivative

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
