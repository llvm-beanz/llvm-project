; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L45 (feme/docs/Roadmap.md): a uniform two-way branch whose arms
; are each barrier-free and reconverge at a merge block -- entirely
; outside every `..._with_group_sync` barrier's own region -- is kept
; intact inside whichever region contains it, rather than being diagnosed
; by `isLinearChain`'s straight-chain check, since it can never itself
; need a region split (see `walkBarrierFreeArm` and `isLinearChain`'s
; "safe diamond" case in EntryWrapper.cpp). Unlike `matchBranchShape`'s
; own "Barrier inside a surviving branch" shape (see
; entry-wrapper-barrier-in-branch.ll), a merge-block phi is fine here: the
; whole diamond, condition, both arms, and the phi, stays inside one
; ordinary per-wave region rather than being hoisted into the wrapper's
; own once-per-group scalar code.

; CHECK-LABEL: define internal void @main(
; CHECK: br i1 %{{.*}}, label %a, label %b
; CHECK: a:
; CHECK: b:
; CHECK: exit:
; CHECK: phi i32 [ %{{.*}}, %a ], [ 0, %b ]
; CHECK: ret void

; CHECK-LABEL: define internal void @main.region0(
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
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %cond = icmp eq i32 %gid, 0
  br i1 %cond, label %a, label %b
a:
  %vala = add i32 %gid, 10
  br label %exit
b:
  br label %exit
exit:
  %val = phi i32 [ %vala, %a ], [ 0, %b ]
  %doubled = mul i32 %val, 2
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
