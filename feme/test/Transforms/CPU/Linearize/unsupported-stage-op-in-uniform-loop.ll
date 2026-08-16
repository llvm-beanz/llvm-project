; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap R27: `feme.stage.discard`/`.demote`/`.is_helper` are only lowered
; inside the same divergent-diamond/divergent-loop-exit shapes
; `DiamondFlattener`/`LoopLinearizer` already support. A loop with no
; divergent exit of its own (an otherwise uniform loop) is a shape neither
; handles yet, so a `feme.stage.discard` inside one is diagnosed rather
; than left for `feme::cpu::SIMDizePass` to silently mis-widen as an
; ordinary opaque call.

; CHECK: feme-cpu-linearize: function 'main': calls a mask-affecting feme.stage.* operation ('discard'/'demote'/'is_helper') in a shape this milestone does not lower (e.g. inside an otherwise uniform loop)
define void @main(i32 %n, i1 %kill) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %loop]
  call void @feme.stage.discard(i1 %kill)
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare void @feme.stage.discard(i1)
attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
