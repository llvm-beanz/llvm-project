; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L420: end-to-end companion to
; `Linearize/loop-divergent-exit-non-phi-exit-relay-value.ll`, confirming
; `feme-cpu-simdize` widens the fixed shadow phi into a genuine per-lane
; `<4 x i1>` instead of leaving the old, buggy shape behind: a scalar
; `phi i1` fed straight to a `zext`/`store` pair, which this milestone's
; own generic widening (driven entirely by a value's *type*, not by
; whether some other pass already laundered its divergence away) then
; happily treated as "already uniform", broadcasting one lane's answer
; group-wide instead of storing each lane's own. Reduced from Clang's own
; `Basic/Mandelbrot.test`'s post-loop `if (Diverged)` color selection,
; confirmed via a real `SIMDization` run to come back group-uniform
; instead of per-lane before this fix (see that test's own comment for
; the full root-cause writeup and provenance).
; CHECK-LABEL: define void @main(
; CHECK: %diverged.result.wide = phi <4 x i1>
; CHECK-NOT: %diverged.result = phi i1
; CHECK: ret void
define void @main(ptr %out, ptr %out2) #0 {
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
  br i1 %trip, label %latch, label %exit
latch:
  br label %header
exit:
  %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
  %diverged.result = phi i1 [ true, %header ], [ false, %body ]
  store i32 %i.result, ptr %out
  %diverged.ext = zext i1 %diverged.result to i32
  store i32 %diverged.ext, ptr %out2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
