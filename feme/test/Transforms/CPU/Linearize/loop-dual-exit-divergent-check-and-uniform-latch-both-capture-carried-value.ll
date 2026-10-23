; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(q): a genuine dual-exit loop -- one separate,
; genuinely uniform `latch`-resident trip-count check *and* one separate,
; divergent `body`-level `break`, both originally targeting `exit`
; directly -- with a loop-carried value (`%v`) read after the loop via a
; phi (`%v.exit`) that already has its own, independent `latch`-incoming
; entry before this pass's `MergedRelayValues` restore loop runs. Before
; this fix, that pre-existing `latch`-incoming entry caused the restore
; loop to unconditionally skip restoring any value this cycle's own
; divergent check had captured for `%v.exit`, silently discarding it:
; `%v.exit` collapsed to a bare `phi i32 [ %v.next, %latch ]`, with the
; `%body`-sourced edge (and the value it should have contributed for any
; lane that broke out via the divergent check) vanished with no
; replacement.
;
; A first attempted fix (a flat, un-frozen
; `select(%exited.via.check.next, %v, %v.next)`, rebuilt fresh every
; round) was itself incomplete: `%v` is `header`'s own raw, continually-
; advancing phi, so a lane that broke on an *earlier* round than the
; group's eventual natural last round would keep having its own captured
; value silently overwritten by `%v`'s own continued (unmasked)
; advancement on every later round. Verified via a 3-round, 4-lane
; by-hand symbolic trace (`%tid` in {0, 1, 2, 3}, `%n` = 3, breaking on
; rounds 0, 1, and 2, with `%tid` 3 never breaking): the flat `select`
; yields `[2, 2, 2, 3]` instead of the correct `[0, 1, 2, 3]`.
;
; The real fix routes the captured check value through
; `freezeExitRelayValue`'s own sticky, self-referential shadow-`phi`
; technique first (`exit.relay.shadow`/`exit.relay.shadow.frozen` below):
; each lane's own captured `%v` value, as of the exact round it took the
; divergent exit, is held fixed through every later round, regardless of
; how many further rounds the rest of the group keeps looping. The same
; 3-round, 4-lane symbolic trace with this fix in place yields the
; correct `[0, 1, 2, 3]`.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %v = phi i32 [ 0, %entry ], [ %v.next, %latch ]
; CHECK: %exited.via.check = phi i1 [ false, %entry ], [ %exited.via.check.next, %latch ]
; CHECK-NEXT: %exit.relay.shadow = phi i32 [ poison, %entry ], [ %exit.relay.shadow.frozen, %latch ]
; CHECK: body:
; CHECK: %exited.via.check.this.iter = and i1
; CHECK: %exited.via.check.next = or i1 %exited.via.check.this.iter, %exited.via.check
; CHECK: latch:
; CHECK: %v.next = add i32 %v, 1
; CHECK: %exit.relay.shadow.frozen = select i1 %exited.via.check.this.iter, i32 %v, i32 %exit.relay.shadow
; CHECK-NEXT: %v.exit.exit.merge = select i1 %exited.via.check.next, i32 %exit.relay.shadow.frozen, i32 %v.next
; CHECK: exit:
; CHECK-NEXT: %v.exit = phi i32 [ %v.exit.exit.merge, %latch ]
define void @main(i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %v = phi i32 [0, %entry], [%v.next, %latch]
  br label %body
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %latch
latch:
  %v.next = add i32 %v, 1
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %header, label %exit
exit:
  %v.exit = phi i32 [%v, %body], [%v.next, %latch]
  %chk = icmp eq i32 %v.exit, 3
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
