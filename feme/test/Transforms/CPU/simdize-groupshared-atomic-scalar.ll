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

; Roadmap L240: each lane's value operand is masked against
; `%wave_sideeffect_mask` (`select i1 %lane.mask, i32 1, i32 0`, using
; `getAtomicRMWIdentity`'s `add`-identity `0` for a masked-off lane) --
; without this, a workgroup whose invocation count isn't a multiple of
; the SIMD wave width would over-execute this `atomicrmw` for the last
; wave's inactive padding lanes, corrupting the accumulator (see
; `widenMaskedAtomicRMW`'s identical, pre-existing masking for
; resource-heap atomics, mirrored here for groupshared ones).

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK-COUNT-4: %lane.mask{{[0-9]*}} = extractelement <4 x i1> %wave_sideeffect_mask, i32 {{[0-9]}}
; CHECK: atomicrmw add ptr %shared.flat{{[0-9]*}}, i32 %lane.val.masked{{[0-9]*}} monotonic
define void @main() #0 {
  %old = atomicrmw add ptr addrspace(3) @shared, i32 1 monotonic
  ret void
}
@shared = internal addrspace(3) global i32 undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
