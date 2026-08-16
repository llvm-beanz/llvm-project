; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; `xchg` has no identity element (any value it writes is observable), so a
; masked-off lane instead reads the value already at the address and writes
; that back -- a real but observably-inert write, matching every other
; `atomicrmw` operation's masking strategy in
; `feme::cpu::SIMDizePass::widenMaskedAtomicRMW` (see
; feme/test/Transforms/CPU/simdize-scalarize-atomic-masked.ll for `add`'s
; identity-element version). This is safe only because dispatch is still
; sequential, one lane at a time (see the "Dispatch is sequential, not
; thread-pooled" P1 narrowing in feme/docs/Roadmap.md's §1.6) -- see
; `widenMaskedAtomicRMW`'s comment.

; CHECK-LABEL: define void @main(
; CHECK: %atomicrmw.mask = and <4 x i1> %wave_sideeffect_mask, %sideeffect.t.wide
; CHECK: %lane.mask = extractelement <4 x i1> %atomicrmw.mask, i32 0
; CHECK-NEXT: %lane.old = load i32, ptr @g
; CHECK-NEXT: %lane.rmw.val = select i1 %lane.mask, i32 7, i32 %lane.old
; CHECK-NEXT: atomicrmw xchg ptr @g, i32 %lane.rmw.val seq_cst
; CHECK-COUNT-3: atomicrmw xchg ptr @g, i32 %{{.*}} seq_cst
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %old = atomicrmw xchg ptr @g, i32 7 monotonic
  br label %end
f:
  br label %end
end:
  ret void
}
@g = global i32 0
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
