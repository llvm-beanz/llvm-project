; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A groupshared `atomicrmw` is accepted by
; `feme::cpu::rewriteGroupSharedGlobals` alongside `load`/`store` (roadmap
; step R2, feme/docs/Roadmap.md's §2.3 `histogram.hlsl`, and see
; `feme/test/Transforms/CPU/simdize-groupshared-uniform.ll` for the
; load/store version this mirrors): its own operands being uniform doesn't
; make the atomic itself skippable (see the "always scalarize an atomicrmw"
; comment in `FunctionWidener::widenInstruction`), so it always reaches
; `feme::cpu::FunctionWidener::widenScalarizedFallback`, which clones it
; once per lane -- each clone a fresh, direct use of `@shared` this pass
; must still rewrite to `wave_groupshared`, "the address space cast away."
; A groupshared *array* element's `atomicrmw` (reached through a
; `getelementptr`, even one with every index constant) is a separate,
; narrower shape -- see simdize-groupshared-atomic-array.ll -- that
; `FunctionWidener::widenGroupSharedAtomicRMW` now also supports (roadmap
; step R23, closing the "access through a getelementptr" gap
; feme/docs/Roadmap.md's §1.6 recorded).

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK-COUNT-4: %shared.flat{{[0-9]*}} = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: atomicrmw add ptr %shared.flat{{[0-9]*}}, i32 1 monotonic
define void @main() #0 {
  %old = atomicrmw add ptr addrspace(3) @shared, i32 1 monotonic
  ret void
}
@shared = internal addrspace(3) global i32 undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
