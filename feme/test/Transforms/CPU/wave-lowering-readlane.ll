; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveReadLaneAt is a guarded extract of the (uniform, per the HLSL source
; rule) requested lane's value, zero if that lane is inactive (see
; WaveLowering.cpp's file comment and WaveCalls.h's `ReadLane`
; documentation for the uniform-index narrowing).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ACTIVE:.*]] = extractelement <4 x i1> %wave_entry_mask, i32 0
; CHECK: %[[VAL:.*]] = extractelement <4 x i32> %tid1, i32 0
; CHECK: select i1 %[[ACTIVE]], i32 %[[VAL]], i32 0
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %val = call i32 @llvm.dx.wave.readlane.i32(i32 %tid, i32 0)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
