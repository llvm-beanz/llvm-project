; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-resources -S %s | FileCheck %s

; feme::amdgpu::ResourceLoweringPass also matches a SPIR-V cbuffer handle --
; `target("spirv.VulkanBuffer", ...)`, the handle `feme::SPIRVToLLVMTranslator`
; raises a `cbuffer` global variable's `spirv.mlir.addressof`/`spirv.Load`
; into (see `ResourceGlobalVariablePattern` in SPIRVToLLVMPatterns.cpp) --
; alongside `spirv.Image`'s own `Buffer`/image-dimension bindings (see
; amdgpu-lower-resources-spirv.ll/amdgpu-lower-resources-spirv-image2d.ll).
; Its accesses take exactly the same shape a `spirv.Image` `Buffer`
; binding's do -- `llvm.spv.resource.getpointer` addressing one element
; (here, one cbuffer field rather than one buffer element), read through
; the single ordinary `load` of it -- so no separate lowering code is
; needed for it; only `collectBindings`' element-type/coordinate-stride
; reads have to key off the handle type name (`spirv.VulkanBuffer` vs
; `spirv.Image`) rather than `ResourceFamily::SPIRV` alone, since the two
; share it but not their handle type's own int parameters' meaning (see
; `SPIRVCBufferResourceOps`'s comment in ResourceLowering.cpp).

target triple = "amdgcn-amd-amdhsa"

; CHECK: define float @cbuffer(ptr addrspace(1) %res.space0.reg0)
; CHECK-NEXT: [[A_PTR:%.*]] = getelementptr float, ptr addrspace(1) %res.space0.reg0, i32 0
; CHECK-NEXT: %a = load float, ptr addrspace(1) [[A_PTR]], align 4
; CHECK-NEXT: [[B_PTR:%.*]] = getelementptr float, ptr addrspace(1) %res.space0.reg0, i32 1
; CHECK-NEXT: %b = load float, ptr addrspace(1) [[B_PTR]], align 4
; CHECK-NEXT: %sum = fadd float %a, %b
; CHECK-NEXT: ret float %sum
define float @cbuffer() #0 {
  %h = call target("spirv.VulkanBuffer", { float, float }, 2, 0)
      @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_sl_f32f32s_2_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %p0 = call ptr addrspace(12) @llvm.spv.resource.getpointer.p12.tspirv.VulkanBuffer_sl_f32f32s_2_0t.i32(
      target("spirv.VulkanBuffer", { float, float }, 2, 0) %h, i32 0)
  %a = load float, ptr addrspace(12) %p0
  %p1 = call ptr addrspace(12) @llvm.spv.resource.getpointer.p12.tspirv.VulkanBuffer_sl_f32f32s_2_0t.i32(
      target("spirv.VulkanBuffer", { float, float }, 2, 0) %h, i32 1)
  %b = load float, ptr addrspace(12) %p1
  %sum = fadd float %a, %b
  ret float %sum
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,8,1" }

declare target("spirv.VulkanBuffer", { float, float }, 2, 0) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_sl_f32f32s_2_0t(i32, i32, i32, i32, ptr)
declare ptr addrspace(12) @llvm.spv.resource.getpointer.p12.tspirv.VulkanBuffer_sl_f32f32s_2_0t.i32(target("spirv.VulkanBuffer", { float, float }, 2, 0), i32)
