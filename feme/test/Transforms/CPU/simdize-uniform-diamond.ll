; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A uniform diamond (both branches converge to a `phi` of two uniform
; constants) is left as ordinary scalar control flow: only the divergent
; `add` that mixes the uniform `phi` result with a thread id gets widened,
; broadcasting the uniform value at its point of use.

; CHECK-LABEL: define void @main(
; CHECK: br i1 %c, label %t, label %f
; CHECK: t:
; CHECK: f:
; CHECK: end:
; CHECK: %p = phi i32 [ 1, %t ], [ 2, %f ]
; CHECK: %p.splat.splat = shufflevector <4 x i32>
; CHECK: add <4 x i32> %p.splat.splat,
define void @main(i32 %uniform_cond) #0 {
entry:
  %c = icmp sgt i32 %uniform_cond, 0
  br i1 %c, label %t, label %f
t:
  br label %end
f:
  br label %end
end:
  %p = phi i32 [1, %t], [2, %f]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %r = add i32 %p, %tid
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
