; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; An `atomicrmw` under a divergent branch's arm is rewritten into a
; `feme.cpu.masked.atomicrmw` call carrying the block's actual mask, the
; same treatment a plain `load`/`store` already gets (see
; `feme/test/Transforms/CPU/Linearize/load-store-masked.ll`) -- roadmap
; milestone 7's "Scalarization fallback does not mask per-lane execution"
; deviation (feme/docs/FeMeCPUDesign.md's Status section): before this fix
; a scalarized atomic ran unconditionally, corrupting memory on behalf of an
; invocation that did not take this path. `feme::cpu::SIMDizePass`'s
; `widenMaskedAtomicRMW` is what actually enforces the mask once widened
; (see `feme/test/Transforms/CPU/simdize-scalarize-atomic-masked.ll`); this
; pass only attaches it.

; CHECK-LABEL: define void @main(
; CHECK: %sideeffect.t = and i1 true, %c
; CHECK: t:
; CHECK: call i32 @feme.cpu.masked.atomicrmw.i32(i32 1, ptr %p, i32 1, i32 4, i1 %sideeffect.t)
define void @main(ptr %p) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %old = atomicrmw add ptr %p, i32 1 monotonic
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
