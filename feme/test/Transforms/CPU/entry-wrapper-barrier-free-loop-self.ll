; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H72 (feme/docs/Roadmap.md): a barrier-free spin-wait loop (the
; shape `dEQP-VK.mesh_shader.ext.misc.{group_memory_barrier,
; memory_barrier_shared}_in_{mesh,task}_*` compile to --
; `while (flags[other] != 1u) { }`, polling a groupshared flag another
; invocation writes) sitting after a `..._with_group_sync` barrier is
; split rather than diagnosed. Unlike `entry-wrapper-barrier-in-loop.ll`'s
; "barrier inside a uniform loop" shape, this loop's own exit condition is
; not a compile-time-computable scalar recurrence at all -- and it
; contains no barrier of its own -- so the whole loop (here, a single
; self-loop block) simply stays inside the region that follows the real
; barrier, running once per wave exactly like ordinary straight-line code
; (see `matchBarrierFreeLoop`'s doc comment in EntryWrapper.cpp).

; CHECK-LABEL: define internal void @main(
; CHECK: spin:
; CHECK: %val = load i32, ptr %{{.*}}
; CHECK-NEXT: %cond = icmp ne i32 %val, 1
; CHECK-NEXT: br i1 %cond, label %spin, label %after
; CHECK: after:
; CHECK-NEXT: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK: store i32 %wave_group_id_x, ptr %ptr0

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header.0:
; CHECK: call void @main.region0(
; CHECK: wave.loop.exit.0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK: wave.loop.header.1:
; CHECK: call void @main(
; CHECK: wave.loop.exit.1:
; CHECK-NEXT: ret void
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %ptr0 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  store i32 %gid, ptr addrspace(3) %ptr0
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  br label %spin

spin:
  %ptr1 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
  %val = load i32, ptr addrspace(3) %ptr1
  %cond = icmp ne i32 %val, 1
  br i1 %cond, label %spin, label %after

after:
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
