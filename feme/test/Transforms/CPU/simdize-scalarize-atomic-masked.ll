; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; An `atomicrmw` inside a divergent diamond's arm (see
; `feme/test/Transforms/CPU/Linearize/resource-call-masked.ll`, run here
; through the actual `feme-cpu-linearize`/`feme-cpu-simdize` pair instead of
; hand-written, to keep this test honest about what the two passes together
; produce): `feme::cpu::LinearizePass` rewrites the plain `atomicrmw` into
; `feme.cpu.masked.atomicrmw.i32` carrying the arm's mask, and
; `feme::cpu::SIMDizePass` widens that into `W` real `atomicrmw`s, each fed
; `Op`'s identity element (`0`, `add`'s) instead of the real operand for a
; lane the mask excludes -- roadmap milestone 7's "Scalarization fallback
; does not mask per-lane execution" deviation this milestone closes (see the
; Status section of feme/docs/FeMeCPUDesign.md): before this fix every lane
; ran the real `atomicrmw add ptr @g, i32 1`, corrupting `@g` on behalf of
; every lane the diamond's condition excluded, not just the ones that took
; this arm.

; CHECK-LABEL: define void @main(
; CHECK: %atomicrmw.mask = and <4 x i1> %wave_sideeffect_mask, %sideeffect.t.wide
; CHECK: %lane.mask = extractelement <4 x i1> %atomicrmw.mask, i32 0
; CHECK-NEXT: %lane.rmw.val = select i1 %lane.mask, i32 1, i32 0
; CHECK-NEXT: atomicrmw add ptr @g, i32 %lane.rmw.val seq_cst
; CHECK-COUNT-3: atomicrmw add ptr @g, i32 %{{.*}} seq_cst
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %old = atomicrmw add ptr @g, i32 1 monotonic
  br label %end
f:
  br label %end
end:
  ret void
}
@g = global i32 0
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
