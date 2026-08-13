; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone 9: a `groupshared` (`addrspace(3)`) array too large for
; the wrapper's stack threshold (`GroupSharedStackLimit`, 16 KiB) falls back
; to the host-supplied `FemeDispatchArgs::GroupShared` buffer instead of an
; `alloca` -- see "Groupshared memory" in "Phase 6: Group Execution and
; Barriers" in feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK-NOT: alloca
; CHECK: getelementptr inbounds nuw {{.*}}, ptr %args, i32 0, i32 8
; CHECK: load ptr, ptr %{{.*}}
define void @main() #0 {
  %ptr = getelementptr inbounds [8192 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [8192 x i32] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
