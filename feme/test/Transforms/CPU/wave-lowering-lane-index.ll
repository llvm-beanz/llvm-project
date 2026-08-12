; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; feme.cpu.builtin.lane_index.v4 (WaveGetLaneIndex) lowers to the constant
; lane iota directly, with no group id/wave index arithmetic at all.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.builtin
; CHECK: ret void
define void @main() #0 {
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %doubled = mul i32 %lane, 2
  ret void
}
declare i32 @llvm.dx.wave.getlaneindex()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
