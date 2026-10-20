; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H125(a): end-to-end companion to
; `Linearize/loop-divergent-exit-carried-value-feeds-uniform-check.ll`,
; confirming `feme-cpu-simdize` no longer rejects this shape with "has a
; divergent branch" after `freezeLoopCarriedValues` stopped corrupting
; `body`'s own, genuinely-uniform `%trip` check's `UniformityInfo`
; classification when freezing `%i` (see that test's comment for the full
; shape rationale and its Clang `Basic/Mandelbrot.test` provenance).
; CHECK-LABEL: define void @main(
; CHECK-NOT: has a divergent branch
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
  br i1 %trip, label %latch, label %exit
latch:
  br label %header
exit:
  %i.result = phi i32 [ %i, %header ], [ %i.next, %body ]
  store i32 %i.result, ptr %out
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
