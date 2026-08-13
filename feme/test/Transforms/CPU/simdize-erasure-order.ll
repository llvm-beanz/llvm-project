; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; `FunctionWidener::widen`'s final erasure pass used to assume `NewF`'s
; block *list* order was itself a "uses before defs" order ("a block always
; precedes what it dominates"), which is not something LLVM guarantees --
; only that a def's block *dominates* its use's block, regardless of where
; either sits in the function's block list. Here `%valD` is defined in `%D`,
; the *last* block in the function's list, but consumed by a divergent
; `select` in `%use`, an *earlier*-listed block that `%D` unconditionally
; falls through to (exactly the shape a `feme::cpu::LinearizePass`-inserted
; "Flow" merge block reproduces in practice, reconverging a value defined
; in a cycle-exit block that sorts later in the list -- see
; `feme-cpu-loop.ll`'s Mandelbrot-shaped coverage). Erasing in list order
; used to delete `%valD` before the `select` that still used it, tripping
; "Use still stuck around after Def is destroyed"; this only checks the
; pass survives and produces the expected widened `select`.

; CHECK-LABEL: define void @main(
; CHECK: select <4 x i1> %cond.wide, <4 x i32> %tid1, <4 x i32> %valD.wide
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  br label %C

use:
  %sel = select i1 %cond, i32 %tid, i32 %valD
  ret void

C:
  %cond = icmp eq i32 %tid, 0
  br label %D

D:
  %valD = add i32 %tid, 1
  br label %use
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
