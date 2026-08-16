; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=16 -S %s 2>&1 | FileCheck %s

; Derivatives and quad ops are only lowered at wave sizes 4 and 8.

; CHECK: feme-cpu-lower-wave: fragment derivatives and quad ops require wave size 4 or 8

define void @ps_main() #0 {
  %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare float @feme.stage.derivative.x.fine.f32(float)

attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="16" }
