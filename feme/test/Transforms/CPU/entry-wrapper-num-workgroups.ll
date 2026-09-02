; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6o: `feme::cpu::EntryWrapperPass` loads `FemeDispatchArgs::
; GroupCount` (`DispatchArgsField::GroupCount`, field index 2 -- right
; after `GroupID`'s own field index 1) and threads its three components
; through to the widened function's own new `wave_group_count_x/y/z`
; parameters -- the dispatch-time source `feme::cpu::SIMDizePass`
; substitutes for `llvm.spv.num.workgroups` (see simdize-num-workgroups.ll)
; -- exactly the way `Args->GroupID` already threads through to
; `wave_group_id_x/y/z`.

; CHECK: define internal void @main(
; CHECK-SAME: i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z,
; CHECK-SAME: i32 %wave_group_count_x, i32 %wave_group_count_y, i32 %wave_group_count_z,
; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: entry:
; CHECK: getelementptr inbounds nuw {{.*}}, ptr %args, i32 0, i32 1
; CHECK: %[[GIDVEC:.*]] = load [3 x i32],
; CHECK-DAG: %[[GIDX:.*]] = extractvalue [3 x i32] %[[GIDVEC]], 0
; CHECK-DAG: %[[GIDY:.*]] = extractvalue [3 x i32] %[[GIDVEC]], 1
; CHECK-DAG: %[[GIDZ:.*]] = extractvalue [3 x i32] %[[GIDVEC]], 2
; CHECK: getelementptr inbounds nuw {{.*}}, ptr %args, i32 0, i32 2
; CHECK: %[[GCVEC:.*]] = load [3 x i32],
; CHECK-DAG: %[[GCX:.*]] = extractvalue [3 x i32] %[[GCVEC]], 0
; CHECK-DAG: %[[GCY:.*]] = extractvalue [3 x i32] %[[GCVEC]], 1
; CHECK-DAG: %[[GCZ:.*]] = extractvalue [3 x i32] %[[GCVEC]], 2
; CHECK: wave.loop.body:
; CHECK: call void @main(i32 %[[GIDX]], i32 %[[GIDY]], i32 %[[GIDZ]], i32 %[[GCX]], i32 %[[GCY]], i32 %[[GCZ]],
define void @main() #0 {
  %nwx = call i32 @llvm.spv.num.workgroups(i32 0)
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %sum = add i32 %tid, %nwx
  ret void
}
declare i32 @llvm.spv.num.workgroups(i32)
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
