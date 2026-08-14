; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Two handles disagreeing about the same (descriptor set, binding)
; identity's element stride (4 bytes vs. 16) are a conflicting declaration
; -- mirroring "conflicting declarations of the same binding are diagnosed"
; in feme/docs/FeMeCPUDesign.md's "Bound-resource normalization" section.
; Both are left as `handlefrombinding` calls rather than normalized --
; `feme::cpu::checkSupportedRaisedOps` still rejects them in the real
; pipeline.

target triple = "spirv-unknown-vulkan-compute"

; CHECK: @llvm.spv.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
define void @a(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 %idx)
  %v = load float, ptr %ptr
  ret void
}

; CHECK: @llvm.spv.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
define void @b(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x <4 x float>], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x <4 x float>], 12, 1) %h, i32 %idx)
  %v = load <4 x float>, ptr %ptr
  ret void
}

; CHECK-NOT: !feme.cpu.bound_resources
