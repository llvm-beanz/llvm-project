; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveActiveAnyTrue/AllTrue reduce a divergent `i1` predicate over the
; active lanes (see WaveLowering.cpp's file comment): `any` is
; `reduce.or(M & X)`; `all` is `reduce.and(select(M, X, true))` -- an
; inactive lane contributes `true`, the identity for `and`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ANDX:.*]] = and <4 x i1> %wave_entry_mask, %pred.wide
; CHECK: call i1 @llvm.vector.reduce.or.v4i1(<4 x i1> %[[ANDX]])
; CHECK: %[[SEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x i1> %pred.wide, <4 x i1> splat (i1 true)
; CHECK: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %[[SEL]])
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %any = call i1 @llvm.dx.wave.any(i1 %pred)
  %all = call i1 @llvm.dx.wave.all(i1 %pred)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.dx.wave.any(i1)
declare i1 @llvm.dx.wave.all(i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
