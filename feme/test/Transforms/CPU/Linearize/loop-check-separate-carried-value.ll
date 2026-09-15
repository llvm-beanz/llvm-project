; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H125: like `loop-divergent-exit-carried-value.ll`, but for the
; "check separate from header/latch" loop shape (see
; `loop-uniform-check-separate-structurized.ll`'s own comment) instead of a
; plain `while` -- exercises `freezeLoopCarriedValues`'s
; `OtherCondBrBlocks`/`CheckBlock` call site in Linearize.cpp rather than
; its `Header != Latch`, no-separate-check-block one. `%r`, read after the
; loop exits, needs its own backedge value frozen once a given lane's own
; `%cond` has gone false, exactly as `loop-divergent-exit-carried-value.
; ll`'s own `%r` does; `%i` (the induction variable, only ever read
; *inside* the loop) is deliberately left unfrozen, same as that test's
; own `%j`.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK-NEXT: %i = phi i32 [ 0, %entry ], [ %inc, %[[BACKEDGE:.*]] ]
; CHECK-NEXT: %r = phi i32 [ 0, %entry ], [ %r.frozen, %[[BACKEDGE]] ]
; CHECK: [[BACKEDGE]]:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK-NEXT: %r.frozen = select i1 %active.check.live, i32 %r.next, i32 %r
; CHECK: exit:
; CHECK: %r, ptr %out
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %r = phi i32 [0, %entry], [%r.next, %latch]
  br label %check
check:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cond = icmp slt i32 %i, %tid
  br i1 %cond, label %body, label %exit
body:
  br label %latch
latch:
  %r.next = add i32 %r, 1
  %inc = add i32 %i, 1
  br label %header
exit:
  store i32 %r, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
