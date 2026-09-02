; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6o: `llvm.spv.num.workgroups` (SPIR-V's `NumWorkgroups` builtin,
; the dispatch's own grid size -- `vkCmdDrawMeshTasksEXT`'s
; `groupCountX/Y/Z`, or `vkCmdDispatch`'s own dimensions) is uniform for
; one widened-function call, exactly like `WorkgroupId`/`llvm.spv.group.id`
; -- the same value for every group in the dispatch -- but, unlike
; `NumSubgroups` (roadmap H6n), it is a genuine *runtime* dispatch-time
; value, not a compile-time constant derivable from `hlsl.numthreads`. It
; therefore substitutes directly for the new `%wave_group_count_x/y/z`
; wave-body parameters (threaded through by `feme::cpu::EntryWrapperPass`
; from `FemeDispatchArgs::GroupCount`, see entry-wrapper-num-workgroups.ll)
; rather than being widened or folded to a constant, mirroring how
; `WorkgroupId` substitutes for `%wave_group_id_x/y/z`.

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z,
; CHECK-SAME: i32 %wave_group_count_x, i32 %wave_group_count_y, i32 %wave_group_count_z,
; CHECK-NOT: call i32 @llvm.spv.num.workgroups(
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.flattened_thread_id_in_group.v4(
; CHECK: add <4 x i32> %[[TID]], %wave_group_count_x.splat.splat
; CHECK: add <4 x i32> %{{.*}}, %wave_group_count_y.splat.splat
; CHECK: add <4 x i32> %{{.*}}, %wave_group_count_z.splat.splat
define void @main() #0 {
  %nwx = call i32 @llvm.spv.num.workgroups(i32 0)
  %nwy = call i32 @llvm.spv.num.workgroups(i32 1)
  %nwz = call i32 @llvm.spv.num.workgroups(i32 2)
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %sumx = add i32 %tid, %nwx
  %sumy = add i32 %sumx, %nwy
  %sumz = add i32 %sumy, %nwz
  ret void
}
declare i32 @llvm.spv.num.workgroups(i32)
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
