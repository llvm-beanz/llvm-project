; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone E13 (`VK_KHR_zero_initialize_workgroup_memory`): a
; `groupshared` (`addrspace(3)`) global carrying an explicit initializer --
; the shape `feme::spirv::WorkgroupGlobalVariablePattern` imports a SPIR-V
; `zero_initializer`'d `Workgroup` variable into (see
; SPIRVToLLVMPatterns.cpp) -- makes `feme::cpu::EntryWrapperPass` zero the
; whole flat groupshared buffer once per group, right after allocating it,
; rather than leaving it as uninitialized stack memory (see
; entry-wrapper-groupshared-stack.ll for the same buffer with no
; zero-initializer, which gets no `memset` at all).

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: %groupshared = alloca [16 x i8], align 4
; CHECK: call void @llvm.memset{{.*}}(ptr align 4 %groupshared, i8 0, i64 16, i1 false)
; CHECK: call void @main({{.*}} ptr %groupshared)
define void @main() #0 {
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [4 x i32] zeroinitializer
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
