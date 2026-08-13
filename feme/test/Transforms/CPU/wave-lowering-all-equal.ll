; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveActiveAllEqual broadcasts the first active lane's value and compares
; every lane's value against it, reducing under the mask the same way
; `wave.all` does (see WaveLowering.cpp's file comment): an inactive lane's
; comparison is forced `true`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[MASKINT:.*]] = bitcast <4 x i1> %wave_entry_mask to i4
; CHECK: %[[ISZERO:.*]] = icmp eq i4 %[[MASKINT]], 0
; CHECK: %[[CTTZ:.*]] = call i4 @llvm.cttz.i4(i4 %[[MASKINT]], i1 false)
; CHECK: select i1 %[[ISZERO]], i4 0, i4 %[[CTTZ]]
; CHECK: %[[FIRST:.*]] = extractelement <4 x i32> %tid1,
; CHECK: %[[CMP:.*]] = icmp eq <4 x i32> %tid1,
; CHECK: %[[SEL:.*]] = select <4 x i1> %wave_entry_mask, <4 x i1> %[[CMP]], <4 x i1> splat (i1 true)
; CHECK: call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %[[SEL]])
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %eq = call i1 @llvm.dx.wave.all.equal.i32(i32 %tid)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i1 @llvm.dx.wave.all.equal.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
