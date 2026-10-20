; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L466: like `loop-divergent-exit-uniform-latch-exit.ll`, but the
; loop's own separate, genuinely uniform exit check sits in some *other*
; block strictly between `header`'s own divergent exit check and `latch`
; (inside `collectUniformPassThroughRegion`'s own uniform pass-through
; region, which that function's own comment already documents as
; tolerating a walk ending at `exit` directly, bypassing `latch`
; entirely) rather than being `latch`'s own terminator. Reduced from
; Clang's own `Basic/Mandelbrot.test`, whose `MaxIteration` hard
; iteration-count cap check is `StructurizeCFG`-relayed through exactly
; such a block (confirmed via `FEME_DUMP_IR_PRESIMD`, distinct from that
; same loop's own `latch` block).
;
; `header`'s own `Frozen` relay value lives in `latch` (see
; `freezeExitRelayValue`'s own comment), which does **not** dominate
; `body`'s own direct edge to `exit` here (`body` is reached without ever
; passing through `latch`): the fix must instead build an equivalent
; blend using `header`-resident values (`active.live`/
; `active.header.live`, both computed at the very top of `header`, which
; dominate everything in this region) and the shadow phi's own `header`-
; resident home, rather than reusing `latch`'s own `Frozen` result
; directly.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %exit.relay.shadow = phi i1 [ poison, %entry ], [ %exit.relay.shadow.frozen, %latch ]
; CHECK: body:
; CHECK: %[[NOT:.+]] = xor i1 %active.header.live, true
; CHECK-NEXT: %exited.via.header.this.iter.bypass = and i1 %active.live, %[[NOT]]
; CHECK-NEXT: %exit.relay.shadow.frozen.bypass = select i1 %exited.via.header.this.iter.bypass, i1 true, i1 %exit.relay.shadow
; CHECK-NEXT: %diverged.via.header.by.now.bypass = xor i1 %active.header.live, true
; CHECK-NEXT: %exit.relay.bypass.blend = select i1 %diverged.via.header.by.now.bypass, i1 %exit.relay.shadow.frozen.bypass, i1 false
; CHECK: exit:
; CHECK-NEXT: %result = phi i1 [ %exit.relay.bypass.blend, %body ], [ %exit.relay.shadow.frozen, %latch ]
define void @main(ptr %out) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %body
body:
  %i.next = add i32 %i, 1
  %trip = icmp slt i32 %i.next, 10
  br i1 %trip, label %cont, label %exit
cont:
  br label %latch
latch:
  br label %header
exit:
  %result = phi i1 [ true, %header ], [ false, %body ]
  store i1 %result, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
