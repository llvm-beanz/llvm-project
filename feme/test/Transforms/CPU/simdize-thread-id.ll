; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Phase 4 (feme::cpu::SIMDizePass) widens a divergent thread-id builtin into
; the canonical `feme.cpu.builtin.*` call (Phase 5 lowers the arithmetic --
; see feme::cpu::BuiltinCalls), and widens the elementwise `mul` that
; consumes it into a `<4 x i32>` operation, broadcasting the uniform
; constant operand.

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z,
; CHECK-SAME: i32 %wave_index, <4 x i1> %wave_entry_mask,
; CHECK-SAME: <4 x i1> %wave_sideeffect_mask, ptr %wave_groupshared
; CHECK: call <4 x i32> @feme.cpu.builtin.thread_id.v4(
; CHECK-SAME: i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z,
; CHECK-SAME: i32 %wave_index, i32 4, i32 1, i32 1, i32 0)
; CHECK: mul <4 x i32> %{{.*}}, splat (i32 2)
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
