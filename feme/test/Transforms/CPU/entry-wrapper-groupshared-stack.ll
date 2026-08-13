; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone 9: a small `groupshared` (`addrspace(3)`) array fits
; under the wrapper's stack threshold, so `feme::cpu::EntryWrapperPass`
; allocates it with an `alloca` rather than reading
; `FemeDispatchArgs::GroupShared` -- see "Groupshared memory" in "Phase 6:
; Group Execution and Barriers" in feme/docs/FeMeCPUDesign.md.
;
; `feme::cpu::SIMDizePass` (Phase 4) already canonicalized the access into a
; `getelementptr` off `wave_groupshared` before this ever runs; see
; simdize-groupshared-uniform.ll for that canonicalization on its own.

; CHECK-LABEL: define internal void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK: %{{.*}} = getelementptr inbounds [4 x i32], ptr %shared.flat, i32 0, i32 0

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: %groupshared = alloca [16 x i8], align 4
; CHECK: call void @main({{.*}} ptr %groupshared)
define void @main() #0 {
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
