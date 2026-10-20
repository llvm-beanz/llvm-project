; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L466: end-to-end companion to
; `Linearize/loop-divergent-exit-uniform-bypass-exit.ll`, confirming
; `feme-cpu-simdize` widens the fixed blend into a genuine per-lane
; `<4 x i1>` instead of leaving the old, buggy shape behind (a scalar
; `phi i1 [ false, %body ]`, with `header`'s own frozen relay value
; silently discarded on this bypass edge, that this milestone's own
; generic widening then happily broadcast group-wide instead of storing
; each lane's own answer). Reduced from Clang's own
; `Basic/Mandelbrot.test`'s post-loop `if (Diverged)` color selection,
; whose `MaxIteration` hard iteration cap check is relayed through a
; block distinct from `latch` (see that test's own comment for the full
; root-cause writeup and provenance).
; CHECK-LABEL: define void @main(
; CHECK: %result.wide = phi <4 x i1>
; CHECK-NOT: %result = phi i1
; CHECK: ret void
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
