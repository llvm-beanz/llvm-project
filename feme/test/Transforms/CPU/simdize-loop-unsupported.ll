; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; A loop is only widenable once its own divergent exit check has gone
; through `feme::cpu::LinearizePass` (roadmap milestone 7): a hand-written
; loop whose exit is still a raw divergent branch (not the
; `feme.cpu.mask.any`-gated backedge the linearizer produces) is diagnosed
; rather than mis-widened, the same as any other unlinearized divergent
; branch (see "Phase 4: Widening").

; CHECK: error: feme-cpu-simdize: function 'main' has a divergent branch
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %next = add i32 %i, 1
  %done = icmp eq i32 %tid, %next
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
