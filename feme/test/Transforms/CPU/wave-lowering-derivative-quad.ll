; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=8 -S %s | FileCheck %s

; Derivatives and quad reads lower in Phase 5 for fragment stages at wave sizes
; 4 and 8, leaving no `feme.stage.derivative.*` or `feme.stage.quad.read`
; declaration behind.

; CHECK-LABEL: define void @ps_main(
; CHECK-NOT: feme.stage.derivative
; CHECK-NOT: feme.stage.quad.read

define void @ps_main() #0 {
  %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
  %qr = call float @feme.stage.quad.read.f32(float %dx, i8 2)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare float @feme.stage.derivative.x.fine.f32(float)
declare float @feme.stage.quad.read.f32(float, i8)

attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="8" }
