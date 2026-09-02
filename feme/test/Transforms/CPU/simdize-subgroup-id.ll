; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6n: `llvm.spv.subgroup.id` ("which subgroup/wave this invocation
; belongs to within its workgroup") is uniform for one widened-function
; call -- it is exactly "which wave-loop iteration this is"
; (feme/docs/FeMeCPUDesign.md's `group = ceil(GroupSize / W) waves`), the
; same value `%wave_index` already threads through the wave-body interface
; for `WorkgroupId` (see feme::cpu::FunctionWidener::replaceGroupIdCall).
; It therefore substitutes directly for `%wave_index` rather than being
; widened at all, mirroring how `WorkgroupId` substitutes for
; `%wave_group_id_x/y/z`.

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z,
; CHECK-SAME: i32 %wave_index,
; CHECK-NOT: call i32 @llvm.spv.subgroup.id()
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.flattened_thread_id_in_group.v4(
; CHECK: add <4 x i32> %[[TID]], %wave_index.splat.splat
define void @main() #0 {
  %sg = call i32 @llvm.spv.subgroup.id()
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %sum = add i32 %tid, %sg
  ret void
}
declare i32 @llvm.spv.subgroup.id()
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
