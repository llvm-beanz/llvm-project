; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap R27: `feme.stage.discard` inside a divergent diamond's arm
; narrows both the live and side-effect masks going forward, and the
; store after it is masked by the narrowed side-effect mask rather than
; left unconditional. See "Shared middle-end phases" in
; feme/docs/FeMeGraphicsDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: %sideeffect.t = and i1 true, %c
; CHECK: t:
; CHECK-NOT: call void @feme.stage.discard
; CHECK: %discard.not = xor i1 %kill, true
; CHECK: %sideeffect.discard = and i1 %sideeffect.t, %discard.not
; CHECK: call void @feme.cpu.masked.store.i32(i32 1, ptr %p, i32 4, i1 %sideeffect.discard)
define void @main(ptr %p, i1 %kill) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  call void @feme.stage.discard(i1 %kill)
  store i32 1, ptr %p
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @feme.stage.discard(i1)
attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
