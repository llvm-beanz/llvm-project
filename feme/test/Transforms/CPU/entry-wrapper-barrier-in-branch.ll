; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R24 (feme/docs/Roadmap.md): a `..._with_group_sync` barrier
; inside a uniform (group-id-derived, so not rejected by
; `feme::cpu::SIMDizePass` itself) branch is split rather than diagnosed --
; see "Barrier inside a surviving branch" in EntryWrapper.cpp's file
; comment. `feme::cpu::matchBranchShape` recognizes the branch's uniform
; condition and each arm's linear chain; `feme::cpu::buildWrapperForBranch`
; clones the condition directly into the wrapper as an ordinary scalar
; `br`, run once for the whole group, and barrier-splits the arm that has
; one (here, the true arm) exactly like a straight-line wave body -- the
; false arm, with no barrier, keeps its single region.

; CHECK-LABEL: define internal void @main.true.body0(
; CHECK: ret void

; CHECK-LABEL: define internal void @main.true.body1(
; CHECK: ret void

; CHECK-LABEL: define internal void @main.false.body0(
; CHECK: ret void

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: branch.cond:
; CHECK-NEXT: %[[COND:[0-9]+]] = icmp eq i32 %{{.*}}, 0
; CHECK-NEXT: br i1 %[[COND]], label %branch.true, label %branch.false

; CHECK: branch.true:
; CHECK-NEXT: br label %wave.loop.header.true.body0

; CHECK: branch.false:
; CHECK-NEXT: br label %wave.loop.header.false.body0

; CHECK: branch.merge:
; CHECK-NEXT: br label %wave.loop.header

; CHECK: call void @main.true.body0(
; CHECK: wave.loop.exit.true.body0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK: call void @main.true.body1(
; CHECK: wave.loop.exit.true.body1:
; CHECK-NEXT: br label %branch.merge

; CHECK: call void @main.false.body0(
; CHECK: wave.loop.exit.false.body0:
; CHECK-NEXT: br label %branch.merge
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %cond = icmp eq i32 %gid, 0
  br i1 %cond, label %a, label %b
a:
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  br label %exit
b:
  br label %exit
exit:
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
