; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap F12a: a std140 uniform buffer array
; (`layout(std140) uniform Input { uint data[16]; } ubo;`, dynamically
; indexed) carries its own real `ArrayStride` (16, wider than its scalar
; `i32` element's own 4-byte natural size) as the handle's own third
; integer parameter -- see
; feme/test/Conversion/SPIRVToLLVM/spirv-to-llvm-glslang-blocks.mlir's own
; `read_std140_element` case for how `feme::spirv::convertUniformArrayContent`
; produces it -- and this pass multiplies the dynamic array index by that
; stride exactly like a storage buffer's own runtime array (see
; spirv-resource-lowering.ll), rather than resolving it to a fixed
; struct-layout byte offset the way a non-array uniform buffer's own
; (always compile-time-constant) field index is.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @main(
; CHECK-SAME: i32 %idx, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define i32 @main(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 2, 0, 16) %h, i32 %idx)
  ; CHECK: [[OFF:%.*]] = zext i32 %idx to i64
  ; CHECK: [[MUL:%.*]] = mul i64 [[OFF]], 16
  %v = load i32, ptr %ptr
  ; CHECK: call i32 @feme.cpu.resource.load.raw.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[MUL]], i1 true)
  ret i32 %v
}

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
; CHECK: !feme.cpu.resources = !{![[RMD:[0-9]+]]}
; CHECK: !feme.cpu.bound_resources = !{![[BMD:[0-9]+]]}
; CHECK: ![[RMD]] = !{!"main", i32 0, i1 false, i32 0, i32 0}
; CHECK: ![[BMD]] = !{!"main", i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 0, i32 0}
