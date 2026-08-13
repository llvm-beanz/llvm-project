; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone 9: `GroupMemoryBarrierWithGroupSync` (raised as
; `llvm.dx.group.memory.barrier.with.group.sync`) requires every invocation
; in the group to arrive before any proceeds, which a single wave loop
; cannot honor on its own -- see "Barriers" in "Phase 6: Group Execution and
; Barriers" in feme/docs/FeMeCPUDesign.md. `feme::cpu::EntryWrapperPass`
; splits the wave body into one region per barrier and wraps each in its
; own wave loop, with a memory fence in between.

; CHECK-LABEL: define internal void @main(
; CHECK-NOT: call void @llvm.dx.group.memory.barrier.with.group.sync
; CHECK: load i32, ptr %{{.*}}
; CHECK: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK: store i32 %{{.*}}, ptr %{{.*}}
; CHECK: ret void

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header.0:
; CHECK: call void @main.region0(
; CHECK: wave.loop.exit.0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK-NEXT: br label %wave.loop.header.1
; CHECK: wave.loop.header.1:
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
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
