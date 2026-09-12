; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H94b (feme/docs/Roadmap.md): same barrier-free-loop shape as
; entry-wrapper-barrier-free-loop-two-block.ll, but with the loop's real
; closing decision reached one level deeper than the arm's own final
; branch -- a shape `feme::cpu::SIMDizePass`'s widening of a
; divergent-trip-count loop (roadmap milestone 4) produces: an outer,
; uniform trip-count check (`header`) whose body arm passes through an
; extra block (`mid`) before that block's own branch closes the loop back
; to `header`, with the arm's final block (`latch`) only reached on the
; other, fresh side of that branch. `walkBarrierFreeArm` must tolerate a
; `CondBr` mid-arm -- not just at the arm's own final terminator -- when
; exactly one of its two successors is already an established block (like
; `isLinearChain`'s own top-level loop-closing case), continuing the walk
; from the other, fresh successor instead of failing outright. Before this
; fix, `mid`'s `CondBr` back to `header` was rejected outright (the prior
; code only accepted a walk ending in an `UncondBrInst`), so the whole
; function was diagnosed as "barrier inside non-linear control flow" even
; though the loop itself contains no barrier at all.

; CHECK-LABEL: define internal void @main(
; CHECK: header:
; CHECK: %cmp = icmp ult i32 %i, 4
; CHECK-NEXT: br i1 %cmp, label %body, label %after
; CHECK: body:
; CHECK-NEXT: br label %mid
; CHECK: mid:
; CHECK: %any = icmp ne i32 %val, 0
; CHECK-NEXT: br i1 %any, label %header, label %latch
; CHECK: latch:
; CHECK-NEXT: %i.next = add i32 %i, 1
; CHECK-NEXT: br label %header
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
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i, %mid ], [ %i.next, %latch ]
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %body, label %after

body:
  br label %mid

mid:
  %ptr1 = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 1
  %val = load i32, ptr addrspace(3) %ptr1
  %any = icmp ne i32 %val, 0
  br i1 %any, label %header, label %latch

latch:
  %i.next = add i32 %i, 1
  br label %header

after:
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
