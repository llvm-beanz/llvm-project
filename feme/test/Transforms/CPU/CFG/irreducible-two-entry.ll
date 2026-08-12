; RUN: feme-opt --llvm -passes=feme-cpu-prepare -verify-structured -S %s -o /dev/null

; The classic irreducible shape: two blocks that are each reachable
; directly from outside the cycle they form together, with no single
; dominating header -- exactly what `FixIrreducible` exists to fix before
; `StructurizeCFG` runs. See "CFG restructurization test suite" in
; feme/docs/FeMeCPUDesign.md.

define void @main(i32 %v) #0 {
entry:
  %c = icmp sgt i32 %v, 0
  br i1 %c, label %a, label %b
a:
  %a.next = sub i32 %v, 1
  %a.done = icmp eq i32 %a.next, 0
  br i1 %a.done, label %exit, label %b
b:
  %b.next = sub i32 %v, 2
  %b.done = icmp eq i32 %b.next, 0
  br i1 %b.done, label %exit, label %a
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
