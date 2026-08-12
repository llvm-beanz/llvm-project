; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; An irreducible two-entry cycle (see irreducible-two-entry.ll) nested
; inside an outer, ordinary loop, so `FixIrreducible` has to fix the inner
; shape without disturbing the outer one. See "CFG restructurization test
; suite" in feme/docs/FeMeCPUDesign.md.

define void @main(i32 %n) #0 {
entry:
  br label %outer
outer:
  %i = phi i32 [0, %entry], [%inc, %outer.latch]
  %outer.cond = icmp slt i32 %i, %n
  br i1 %outer.cond, label %inner.entry, label %exit
inner.entry:
  br i1 %outer.cond, label %a, label %b
a:
  %a.done = icmp eq i32 %i, 3
  br i1 %a.done, label %outer.latch, label %b
b:
  %b.done = icmp eq i32 %i, 5
  br i1 %b.done, label %outer.latch, label %a
outer.latch:
  %inc = add i32 %i, 1
  br label %outer
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
