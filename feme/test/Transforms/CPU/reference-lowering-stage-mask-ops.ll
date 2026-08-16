; RUN: feme-opt --llvm -passes=feme-cpu-reference-lower-builtins -S %s | FileCheck %s

; Roadmap R27: `--reference` mode's counterpart to `feme::cpu::LinearizePass`'s
; `feme.stage.discard`/`.demote`/`.is_helper` lowering. One invocation at a
; time has no live/side-effect mask to narrow, so `discard` becomes a real
; conditional early return, and `demote`/`is_helper` read/write a
; per-invocation `helper` flag instead. See the "Deviation" note in
; ReferenceLowering.h: unlike the widened path, a `demote`d invocation's
; later side effects are not yet suppressed.

; CHECK-LABEL: define void @main(
; CHECK: %reference.helper = alloca i1
; CHECK: store i1 false, ptr %reference.helper
; CHECK: br i1 %cond, label %[[KILLED:.*]], label %[[CONT:.*]]
; CHECK: [[KILLED]]:
; CHECK-NEXT: ret void
; CHECK: [[CONT]]:
; CHECK: %reference.helper.old = load i1, ptr %reference.helper
; CHECK: %reference.helper.next = or i1 %reference.helper.old, %cond
; CHECK: store i1 %reference.helper.next, ptr %reference.helper
; CHECK: %reference.is_helper = load i1, ptr %reference.helper
; CHECK-NOT: call {{.*}} @feme.stage
define void @main(i1 %cond) #0 {
entry:
  call void @feme.stage.discard(i1 %cond)
  call void @feme.stage.demote(i1 %cond)
  %h = call i1 @feme.stage.is_helper()
  ret void
}
declare void @feme.stage.discard(i1)
declare void @feme.stage.demote(i1)
declare i1 @feme.stage.is_helper()
attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
