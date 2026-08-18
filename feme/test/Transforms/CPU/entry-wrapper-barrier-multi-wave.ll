; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; V3: "Verify workgroup barrier correctness for multi-wave groups under
; sequential wave execution" (FeMeVulkanDesign.md's V3 milestone). Combines
; entry-wrapper-barrier-region-split.ll's barrier split with
; entry-wrapper-multi-wave.ll's two-wave group (`NumThreads = (8, 1, 1)`,
; `W = 4`): every existing barrier-splitting test uses a single-wave group,
; so none of them can tell a wave loop that correctly runs *both* waves
; through one region before advancing to the next from one that
; (incorrectly) let a single wave run every region before the next wave
; started any -- exactly the ordering `GroupMemoryBarrierWithGroupSync`
; exists to forbid (see "Barriers" in "Phase 6: Group Execution and
; Barriers" in feme/docs/FeMeCPUDesign.md). Each of the two regions below
; gets its own wave loop, and each loop's own trip count is the group's
; full wave count (`icmp ult i32 %w, 2`), not 1 -- proof that splitting and
; multi-wave dispatch compose rather than one silently assuming
; single-wave groups.

; CHECK-LABEL: define internal void @main(
; CHECK-NOT: call void @llvm.dx.group.memory.barrier.with.group.sync
; CHECK: load i32, ptr %{{.*}}
; CHECK: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK: store i32 %{{.*}}, ptr %{{.*}}
; CHECK: ret void

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header.0:
; CHECK: %w.0 = phi i32 [ 0, %{{.*}} ], [ %w.next.0, %{{.*}} ]
; CHECK: %wave.cond.0 = icmp ult i32 %w.0, 2
; CHECK: call void @main.region0(
; CHECK: wave.loop.exit.0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK-NEXT: br label %wave.loop.header.1
; CHECK: wave.loop.header.1:
; CHECK: %w.1 = phi i32 [ 0, %{{.*}} ], [ %w.next.1, %{{.*}} ]
; CHECK: %wave.cond.1 = icmp ult i32 %w.1, 2
; CHECK: call void @main(
; CHECK: wave.loop.exit.1:
; CHECK-NEXT: ret void
define void @main() #0 {
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %ptr0 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  store i32 %gid, ptr addrspace(3) %ptr0
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %ptr1 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
  %val = load i32, ptr addrspace(3) %ptr1
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
