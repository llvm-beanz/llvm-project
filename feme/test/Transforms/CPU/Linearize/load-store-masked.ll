; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A plain `store` under a divergent branch's arm is rewritten into a
; `feme.cpu.masked.store` call carrying the block's actual mask, so it never
; touches memory on behalf of an invocation that did not take this path (see
; "Side-effecting operations ... are rewritten into the masked intrinsic
; forms" in "Phase 3: Linearization and Predication" in
; feme/docs/FeMeCPUDesign.md). A `load` gets the same treatment ("Loads from
; addresses that could be lane-varying get the same treatment"), even though
; this particular load's address happens to be uniform -- Phase 4 is what
; decides, from the address's own uniformity, how such a masked op actually
; lowers.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c
; CHECK: %sideeffect.t = and i1 true, %c
; CHECK: t:
; CHECK: %loaded1 = call i32 @feme.cpu.masked.load.i32(ptr %p, i32 4, i1 %live.t, i32 0)
; CHECK: call void @feme.cpu.masked.store.i32(i32 %loaded1, ptr %p, i32 4, i1 %sideeffect.t)
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %loaded = load i32, ptr %p
  store i32 %loaded, ptr %p
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
