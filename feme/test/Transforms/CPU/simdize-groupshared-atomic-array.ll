; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R23: a groupshared *array* element's `atomicrmw` (reached
; through a `getelementptr`, even one with every index constant) closes
; the "access through a getelementptr" shape feme/docs/Roadmap.md's §1.6
; recorded (see simdize-groupshared-atomic-scalar.ll's comment for the
; narrower, direct-global-only case this generalizes). The atomic still
; always executes once per lane (its own operands being uniform doesn't
; make it skippable), but `FunctionWidener::widenGroupSharedAtomicRMW`
; reuses the uniform `getelementptr` directly for every lane's clone
; instead of `getWidened`'s usual broadcast-then-extract, which -- unlike
; a direct, unindexed global reference (a `Constant`, broadcast and folded
; straight back to itself) -- would otherwise leave a real
; `insertelement`/`shufflevector` `feme::cpu::rewriteGroupSharedGlobals`
; cannot see through.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %ptr{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %shared.flat, i32 0, i32 2
; CHECK-COUNT-4: atomicrmw add ptr %ptr{{[0-9]*}}, i32 1 monotonic
define void @main() #0 {
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 2
  %old = atomicrmw add ptr addrspace(3) %ptr, i32 1 monotonic
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
