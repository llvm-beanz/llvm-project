; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H125: `%r`, a loop-carried value read *after* the loop exits
; (neither it nor `%j` is a wave call's operand, so `applyStageMasks`'s own
; reduce-masking from roadmap H124h never applies here), computed inside a
; loop whose own exit condition is divergent (a per-lane variable trip
; count, like `%j < %tid` here), needs its own backedge value frozen once a
; given lane's own real trip count is exhausted -- see
; `freezeLoopCarriedValues`'s own comment in Linearize.cpp. Before this
; fix, `%r`'s backedge value came straight from the body's own
; unconditional recomputation every "wide" iteration, silently corrupting
; the value for any lane whose own `%cond` had already gone false on an
; earlier iteration but whose wave still had other, still-looping lanes
; keeping the whole flattened loop going. Reduced from
; `WaveActiveBitXor.convergence.test`'s own `ExpectedOut5`/`Out5` shader
; body (the wave-reduce call itself is not needed to reproduce this: a
; plain `add` loop body hits the identical gap). `%j` (the loop's own
; induction variable, only ever read *inside* the loop) is deliberately
; left unfrozen -- see the same comment's discussion of why only a phi
; read after the loop needs this treatment.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK-NEXT: %r = phi i32 [ 0, %entry ], [ %r.frozen, %latch ]
; CHECK-NEXT: %j = phi i32 [ 0, %entry ], [ %j.next, %latch ]
; CHECK: %active.header.live = and i1 %active.live, %cond
; CHECK: latch:
; CHECK: %r.next = add i32 %r, 1
; CHECK: %j.next = add i32 %j, 1
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header.live)
; CHECK-NEXT: %r.frozen = select i1 %active.header.live, i32 %r.next, i32 %r
; CHECK: br i1 %loop.any.active, label %header, label %exit
; CHECK: exit:
; CHECK-NEXT: store i32 %r, ptr %out
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %r = phi i32 [0, %entry], [%r.next, %latch]
  %j = phi i32 [0, %entry], [%j.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cond = icmp ult i32 %j, %tid
  br i1 %cond, label %latch, label %exit
latch:
  %r.next = add i32 %r, 1
  %j.next = add i32 %j, 1
  br label %header
exit:
  store i32 %r, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
