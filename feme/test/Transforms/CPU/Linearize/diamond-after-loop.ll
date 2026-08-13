; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A divergent diamond entirely *after* a loop, branching on a value the
; loop computed -- the shape a Mandelbrot-style escape-time loop followed
; by a palette lookup produces. `feme::cpu::DiamondFlattener` used to stop
; its traversal for good the moment it reached the loop (a cycle), so this
; diamond -- reachable only from the loop's exit block, never from the
; function entry without crossing the loop -- was silently never visited
; at all. It now also validates/flattens starting from each cycle's exit
; block(s), in addition to the function entry, so this diamond is
; flattened just like any other. See "Loops with a divergent exit" in
; feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: loop:
; CHECK: br i1 %loop.cond, label %loop, label %exit
; CHECK: exit:
; CHECK: br label %t
; CHECK: t:
; CHECK-NEXT: %a = add i32 %i, 1
; CHECK-NEXT: br label %f
; CHECK: f:
; CHECK-NEXT: %b = add i32 %i, 2
; CHECK-NEXT: br label %end
; CHECK: end:
; CHECK-NEXT: %v.linearized = select i1 %c, i32 %a, i32 %b
; CHECK-NOT: phi
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %loop]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, %i
  br i1 %c, label %t, label %f
t:
  %a = add i32 %i, 1
  br label %end
f:
  %b = add i32 %i, 2
  br label %end
end:
  %v = phi i32 [%a, %t], [%b, %f]
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
