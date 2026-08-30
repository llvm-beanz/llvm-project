; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap H13d: the shape `ResourceAddressOfPattern`
; (SPIRVToLLVMPatterns.cpp) produces for an ordinary GLSL `uniform
; sampler2D` declaration -- a single `OpTypeSampledImage` `UniformConstant`
; variable, with no separate `OpSampledImage`/two independently-declared
; handles -- rather than the two-handle, `insertvalue`/`extractvalue`-linked
; shape `spirv-resource-lowering-image.ll` covers. A single
; `handlefrombinding` call already returns the combined `{image, sampler}`
; struct directly; `foldSampledImageStructs` alone cannot fold this (there
; is no `insertvalue` chain for `FindInsertedValue` to trace), so
; `splitCombinedSampledImageHandles` splits it into two ordinary handles
; first. Both share the combined descriptor's own (set, binding) -- (0, 0)
; here -- exactly like the real `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`
; binding they came from, which `RangeKey`'s new `Class` field (roadmap
; H13d) keeps from colliding as a false conflicting re-declaration.

target triple = "spirv-unknown-vulkan-compute"

%sampled_image = type { target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler") }

; CHECK-LABEL: define <4 x float> @sample(
; CHECK-SAME: <2 x float> %coord, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define <4 x float> @sample(<2 x float> %coord) {
  %h = call %sampled_image
      @llvm.spv.resource.handlefrombinding.tsi(i32 0, i32 0, i32 1, i32 0, ptr null)
  %i = extractvalue %sampled_image %h, 0
  %s = extractvalue %sampled_image %h, 1
  ; CHECK: %[[U:.*]] = extractelement <2 x float> %coord, i64 0
  ; CHECK: %[[V:.*]] = extractelement <2 x float> %coord, i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.sample.2d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[U]], float %[[V]], float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 false, i1 true)
  %r = call <4 x float> @llvm.spv.resource.sample(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %i,
      target("spirv.Sampler") %s, <2 x float> %coord, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; Neither the combined handle call nor its `extractvalue`s survive the
; split -- only the two synthetic, separately-typed handles do.

; CHECK-NOT: extractvalue
; CHECK-NOT: call %sampled_image

declare %sampled_image
    @llvm.spv.resource.handlefrombinding.tsi(i32, i32, i32, i32, ptr)

; A combined image+sampler binding reserves one slot in *both* the image
; and sampler heaps despite sharing a single (set, binding) -- confirming
; `RangeKey`'s new `Class` field kept the two from being flagged a
; conflicting re-declaration of the same identity.

; CHECK: !{!"sample", i32 0, i1 true, i32 0, i32 0}
; CHECK: !{!"sample", i32 0, i32 1, i32 1, i32 0, i32 0, i32 1, i32 0, i32 1, i32 0, i32 0, i32 1, i32 0, i32 2}
