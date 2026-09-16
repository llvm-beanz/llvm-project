; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H158 (feme/docs/Roadmap.md): a "Flow-merge loop" whose body
; ends in a uniform `CondBr` forming a "safe diamond" -- both arms
; barrier-free, reconverging at one common merge block -- rather than an
; unconditional branch. Such a diamond can never need a region split of
; its own, so it always lands entirely inside whichever single region
; contains it; it just has to be walked through, arms and all, when the
; chain is rebuilt after the barrier splits.

; The diamond is outlined whole into the first region, keeping its own
; internal branch and merge phi.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: body:
; CHECK: br i1 %{{.*}}, label %then, label %flow
; CHECK: then:
; CHECK: flow:
; CHECK: phi i32
; CHECK: ret void

; The barrier still splits the loop body right after the diamond.
; CHECK-LABEL: define internal void @main.body1(

define void @main() #0 {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %body, label %after

body:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %cond2 = icmp ugt i32 %i, 0
  br i1 %cond2, label %then, label %flow

then:
  %tmp = add i32 %gid, 1
  br label %flow

flow:
  %merged = phi i32 [ %gid, %body ], [ %tmp, %then ]
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  br label %header

after:
  ret void
}

declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
