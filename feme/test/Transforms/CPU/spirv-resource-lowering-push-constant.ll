; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers the "combined" case feme::cpu::SPIRVPushConstantLowering.h's file
; comment describes: a function using both a bound `spirv.VulkanBuffer`
; handle and a push-constant block. `feme::cpu::SPIRVResourceLoweringPass`
; itself lowers the push-constant access too, reusing the
; `root_constants`/`root_constant_size` parameters it already
; unconditionally appends, rather than leaving that to a second pass that
; would otherwise add a second, colliding pair.

target triple = "spirv-unknown-vulkan-compute"

%PushConstants = type { i32 }
@pc = external addrspace(13) constant %PushConstants

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define void @main() {
  %h = call target("spirv.VulkanBuffer", [0 x float], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x float], 12, 1) %h, i32 0)

  %pcp = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 0
  %scale = load i32, ptr addrspace(13) %pcp
  ; CHECK: %push_const.inbounds = icmp ule i32 4, %root_constant_size
  ; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 0
  ; CHECK: %push_const.load = load i32, ptr %push_const.ptr
  %scale_f = sitofp i32 %scale to float

  %v = load float, ptr %ptr
  %v2 = fmul float %v, %scale_f
  ; CHECK: call float @feme.cpu.resource.load.raw.f32(
  store float %v2, ptr %ptr
  ; CHECK: call void @feme.cpu.resource.store.raw.f32(
  ret void
}

; CHECK: !feme.cpu.resources = !{![[RMD:[0-9]+]]}
; CHECK: ![[RMD]] = !{!"main", i32 4, i1 false, i32 0, i32 0}
