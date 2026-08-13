; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveGetLaneCount lowers to the constant wave size directly (see
; WaveLowering.cpp's file comment): no cross-lane arithmetic, no mask, no
; operand.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %doubled = mul i32 4, 2
; CHECK: %use.wide = add <4 x i32>
define void @main() #0 {
  %n = call i32 @llvm.dx.wave.get.lane.count()
  %doubled = mul i32 %n, 2
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %use = add i32 %doubled, %tid
  ret void
}
declare i32 @llvm.dx.wave.get.lane.count()
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
