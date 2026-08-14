; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveReadLaneAt lowers to a per-lane guarded extract, gathering source
; lane `I[L]`'s value for each output lane `L` (see WaveLowering.cpp's file
; comment and `lowerReadLane`): a uniform lane index (as here) simply means
; every iteration happens to extract the same source lane, still zero if
; that lane is inactive. See wave-lowering-readlane-varying.ll for a
; genuinely varying lane index.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ACTIVE0:.*]] = extractelement <4 x i1> %wave_entry_mask, i32 0
; CHECK: %[[VAL0:.*]] = extractelement <4 x i32> %tid1, i32 0
; CHECK: %[[SEL0:.*]] = select i1 %[[ACTIVE0]], i32 %[[VAL0]], i32 0
; CHECK: insertelement <4 x i32> poison, i32 %[[SEL0]], i32 0
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %val = call i32 @llvm.dx.wave.readlane.i32(i32 %tid, i32 0)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.readlane.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
