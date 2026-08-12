; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Phase 5's builtin half (feme::cpu::WaveLoweringPass) lowers the
; feme.cpu.builtin.thread_id.v4 call Phase 4 introduced into real
; <4 x i32> arithmetic over the wave-body's group id/wave index parameters
; and a constant lane iota (see WaveLowering.cpp's file comment): with
; NumThreads = (4, 1, 1) and W = 4, one wave covers exactly one group, so
; thread_id.x reduces to group_id.x * 4 + ((wave_index * 4 + lane) % 4).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.builtin
; CHECK: %[[FLAT:.*]] = add <4 x i32> %{{.*}}, <i32 0, i32 1, i32 2, i32 3>
; CHECK: urem <4 x i32> %[[FLAT]], splat (i32 4)
; CHECK: %doubled.wide = mul <4 x i32> %tid1, splat (i32 2)
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
