; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveActiveBallot is `bitcast (M & X) to iW`, split and zero-pad into the
; source ABI's 32-bit result words (see WaveLowering.cpp's file comment and
; `lowerBallot`) -- roadmap step R3's ballot.hlsl end-to-end test exercises
; this through the full CPU pipeline; this test isolates the lowering itself.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ANDX:.*]] = and <4 x i1> %wave_entry_mask, %pred.wide
; CHECK: %[[ASINT:.*]] = bitcast <4 x i1> %[[ANDX]] to i4
; CHECK: %[[WORD0:.*]] = zext i4 %[[ASINT]] to i32
; CHECK: %[[V0:.*]] = insertvalue { i32, i32, i32, i32 } poison, i32 %[[WORD0]], 0
; CHECK: %[[V1:.*]] = insertvalue { i32, i32, i32, i32 } %[[V0]], i32 0, 1
; CHECK: %[[V2:.*]] = insertvalue { i32, i32, i32, i32 } %[[V1]], i32 0, 2
; CHECK: insertvalue { i32, i32, i32, i32 } %[[V2]], i32 0, 3
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %r = call {i32,i32,i32,i32} @llvm.dx.wave.ballot.i32(i1 %pred)
  %x = extractvalue {i32,i32,i32,i32} %r, 0
  %cnt = call i32 @llvm.ctpop.i32(i32 %x)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare {i32,i32,i32,i32} @llvm.dx.wave.ballot.i32(i1)
declare i32 @llvm.ctpop.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
