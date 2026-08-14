; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Two shapes feme::cpu::SPIRVResourceLoweringPass doesn't (yet) canonicalize
; leave the whole function untouched rather than being partially rewritten
; -- see the "Scope" note in
; feme/include/feme/Transforms/CPU/SPIRVResourceLowering.h: an image/sampler
; handle (`Buffer<T>`/`RWBuffer<T>` -- a texture, not a storage buffer), and
; a storage-buffer element accessed through a further `getelementptr` into
; its own fields (a `StructuredBuffer` with more than one field, read
; through one of them individually).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @image_resource(
; CHECK-NOT: resource_heap
; CHECK: call target("spirv.Image", {{.*}}) @llvm.spv.resource.handlefrombinding
define void @image_resource(i32 %idx) {
  %h = call target("spirv.Image", float, 5, 2, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", float, 5, 2, 0, 0, 2, 1) %h, i32 %idx)
  %v = load <4 x float>, ptr %ptr
  ret void
}

; CHECK-LABEL: define void @field_access(
; CHECK-NOT: resource_heap
; CHECK: call target("spirv.VulkanBuffer", {{.*}}) @llvm.spv.resource.handlefrombinding
define void @field_access(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x {<4 x i32>, <4 x float>}], 12, 0)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(
          target("spirv.VulkanBuffer", [0 x {<4 x i32>, <4 x float>}], 12, 0) %h, i32 %idx)
  %field = getelementptr inbounds {<4 x i32>, <4 x float>}, ptr %ptr, i32 0, i32 1
  %v = load <4 x float>, ptr %field
  ret void
}

; CHECK-NOT: !feme.cpu.bound_resources
