; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H6n: `llvm.spv.num.subgroups` ("how many subgroups/waves this
; workgroup dispatches as") folds directly to a compile-time constant --
; `ceil(NumThreadsX * NumThreadsY * NumThreadsZ / WaveSize)`, mirroring
; feme/docs/FeMeCPUDesign.md's own `group = ceil(GroupSize / W) waves`
; formula for the wave loop's own trip count -- rather than being widened
; or threaded through the wave-body interface at all. `hlsl.numthreads` is
; "10,1,1" and `-feme-cpu-wave-size=4` here, so `ceil(10 / 4) == 3`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: call i32 @llvm.spv.num.subgroups()
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.flattened_thread_id_in_group.v4(
; CHECK: add <4 x i32> %[[TID]], splat (i32 3)
define void @main() #0 {
  %ns = call i32 @llvm.spv.num.subgroups()
  %tid = call i32 @llvm.spv.flattened.thread.id.in.group()
  %sum = add i32 %tid, %ns
  ret void
}
declare i32 @llvm.spv.num.subgroups()
declare i32 @llvm.spv.flattened.thread.id.in.group()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="10,1,1" }
