; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; WaveIsFirstLane's result is divergent (a different answer per lane, see
; WaveLowering.cpp's file comment): `M != 0 && lane == cttz(bitcast M to
; iW, false)`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[MASKINT:.*]] = bitcast <4 x i1> %wave_entry_mask to i4
; CHECK: %[[ISZERO:.*]] = icmp eq i4 %[[MASKINT]], 0
; CHECK: call i4 @llvm.cttz.i4(i4 %[[MASKINT]], i1 false)
; CHECK: icmp eq <4 x i32> <i32 0, i32 1, i32 2, i32 3>,
; CHECK: xor i1 %[[ISZERO]], true
; CHECK: %first{{.*}} = and <4 x i1>
define void @main() #0 {
  %first = call i1 @llvm.dx.wave.is.first.lane()
  %sel = select i1 %first, i32 1, i32 0
  ret void
}
declare i1 @llvm.dx.wave.is.first.lane()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
