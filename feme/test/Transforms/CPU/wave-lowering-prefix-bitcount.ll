; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WavePrefixBitCount's result is divergent (an exclusive running count of
; the active-and-true lanes before each lane), built as an unrolled lane
; loop rather than a shuffle scan (see WaveLowering.cpp's file comment for
; why that is correct and sufficient at these wave sizes): lane 0 always
; sees a running count of 0, and each later lane adds the previous lane's
; (masked) bit.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ANDX:.*]] = and <4 x i1> %wave_entry_mask, %pred.wide
; CHECK: %[[BIT0:.*]] = extractelement <4 x i1> %[[ANDX]], i32 0
; CHECK: %[[BIT0I32:.*]] = zext i1 %[[BIT0]] to i32
; CHECK: %[[SUM1:.*]] = add i32 0, %[[BIT0I32]]
; CHECK: insertelement <4 x i32> <i32 0, i32 poison, i32 poison, i32 poison>, i32 %[[SUM1]], i32 1
; CHECK: %[[BIT1:.*]] = extractelement <4 x i1> %[[ANDX]], i32 1
; CHECK: %[[BIT1I32:.*]] = zext i1 %[[BIT1]] to i32
; CHECK: %[[SUM2:.*]] = add i32 %[[SUM1]], %[[BIT1I32]]
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %cnt = call i32 @llvm.dx.wave.prefix.bit.count(i1 %pred)
  %doubled = mul i32 %cnt, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.prefix.bit.count(i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
