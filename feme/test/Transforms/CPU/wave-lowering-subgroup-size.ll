; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; V4: llvm.spv.subgroup.size (Vulkan's SubgroupSize builtin) lowers exactly
; like llvm.dx.wave.get.lane.count -- the constant wave size directly, no
; cross-lane arithmetic (see WaveLowering.cpp's file comment and
; SIMDize.cpp's classifyWaveCall comment for why they share
; WaveCallKind::GetLaneCount).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %doubled = mul i32 4, 2
; CHECK: %use.wide = add <4 x i32>
define void @main() #0 {
  %n = call i32 @llvm.spv.subgroup.size()
  %doubled = mul i32 %n, 2
  %tid = call i32 @llvm.spv.thread.id(i32 0)
  %use = add i32 %doubled, %tid
  ret void
}
declare i32 @llvm.spv.subgroup.size()
declare i32 @llvm.spv.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
