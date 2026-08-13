; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveAllBitCount (WaveActiveCountBits) is `ctpop(bitcast (M & X) to iW)`
; (see WaveLowering.cpp's file comment).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ANDX:.*]] = and <4 x i1> %wave_entry_mask, %pred.wide
; CHECK: %[[ASINT:.*]] = bitcast <4 x i1> %[[ANDX]] to i4
; CHECK: call i4 @llvm.ctpop.i4(i4 %[[ASINT]])
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %cnt = call i32 @llvm.dx.wave.active.countbits(i1 %pred)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.active.countbits(i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
