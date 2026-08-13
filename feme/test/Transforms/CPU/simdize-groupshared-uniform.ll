; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone 9: `feme::cpu::SIMDizePass` canonicalizes a uniform
; access to a `groupshared` (`addrspace(3)`) global into a `getelementptr`
; off the widened function's `wave_groupshared` parameter, "the address
; space cast away" (see GroupShared.h and "Groupshared memory" in "Phase 6:
; Group Execution and Barriers" in feme/docs/FeMeCPUDesign.md). This is a
; canonicalization step only -- allocating the buffer itself is
; `feme::cpu::EntryWrapperPass`'s job (see entry-wrapper-groupshared-stack.ll).

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %{{.*}} = getelementptr inbounds [4 x i32], ptr %shared.flat, i32 0, i32 0
; CHECK-NEXT: %val = load i32, ptr %{{.*}}, align 4
define void @main() #0 {
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
