; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R5 (feme/docs/Roadmap.md): a `..._with_group_sync` barrier
; inside a uniform loop (the "stride-halving reduction" shape `reduction.hlsl`
; compiles to) is split rather than diagnosed -- see "Barriers inside a
; uniform loop" in EntryWrapper.cpp's file comment. The loop's own header
; (phi + comparison) and latch (the `stride >>= 1` recurrence) are cloned
; directly into the wrapper as an ordinary scalar loop, run once per
; iteration; the barrier-split body regions each still run once per wave,
; once per iteration, with a fence between them. The loop-carried `stride`
; itself becomes a `loopvarN` parameter every region (even the trivial
; prefix/suffix ones) is called with, and the divergent-looking `sum` value
; that crosses the barrier is spilled exactly like
; entry-wrapper-barrier-live-value-spill.ll's straight-line case.

; CHECK-LABEL: define internal void @main.body0(
; CHECK-SAME: i32 %loopvar0, ptr %barrier_spill)
; CHECK: %sum = add i32 %wave_group_id_x, %loopvar0
; CHECK: store i32 %sum, ptr %sum.spill

; CHECK-LABEL: define internal void @main.body1(
; CHECK-SAME: i32 %loopvar0, ptr %barrier_spill)
; CHECK: %sum.reload.val = load i32, ptr %sum.reload
; CHECK: %doubled = mul i32 %sum.reload.val, 2

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: br label %wave.loop.header
; CHECK: wave.loop.exit:
; CHECK-NEXT: br label %loop.header
; CHECK: loop.header:
; CHECK-NEXT: %loopvar0 = phi i32 [ 2, %wave.loop.exit ], [ %[[NEXT:[0-9]+]], %loop.latch ]
; CHECK-NEXT: %[[COND:[0-9]+]] = icmp ugt i32 %loopvar0, 0
; CHECK-NEXT: br i1 %[[COND]], label %loop.body.iter, label %loop.exit
; CHECK: loop.body.iter:
; CHECK-NEXT: br label %wave.loop.header.body0
; CHECK: loop.latch:
; CHECK-NEXT: %[[NEXT]] = lshr i32 %loopvar0, 1
; CHECK-NEXT: br label %loop.header
; CHECK: call void @main.body0(
; CHECK: wave.loop.exit.body0:
; CHECK-NEXT: fence syncscope("singlethread") acq_rel
; CHECK: call void @main.body1(
; CHECK: wave.loop.exit.body1:
; CHECK-NEXT: br label %loop.latch
; CHECK: call void @main.suffix(
define void @main() #0 {
entry:
  br label %loop.header
loop.header:
  %stride = phi i32 [ 2, %entry ], [ %stride.next, %loop.latch ]
  %cond = icmp ugt i32 %stride, 0
  br i1 %cond, label %loop.body, label %loop.exit
loop.body:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %sum = add i32 %gid, %stride
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %doubled = mul i32 %sum, 2
  br label %loop.latch
loop.latch:
  %stride.next = lshr i32 %stride, 1
  br label %loop.header
loop.exit:
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
