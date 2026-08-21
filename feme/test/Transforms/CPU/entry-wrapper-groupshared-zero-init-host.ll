; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone E13 (`VK_KHR_zero_initialize_workgroup_memory`): a
; zero-initializer'd `groupshared` global too large for the wrapper's own
; stack (see entry-wrapper-groupshared-host.ll) still gets zeroed, once per
; group, right after loading `FemeDispatchArgs::GroupShared` -- this
; buffer is otherwise reused as-is across every group in the same dispatch
; (see `runDispatch` in feme/lib/Vulkan/CommandBuffer.cpp), so a later
; group would otherwise see an earlier group's own leftover contents.

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK-NOT: alloca
; CHECK: getelementptr inbounds nuw {{.*}}, ptr %args, i32 0, i32 3
; CHECK: [[BUF:%.*]] = load ptr, ptr %{{.*}}
; CHECK: call void @llvm.memset{{.*}}(ptr align 16 [[BUF]], i8 0, i64 32768, i1 false)
define void @main() #0 {
  %ptr = getelementptr inbounds [8192 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [8192 x i32] zeroinitializer
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
