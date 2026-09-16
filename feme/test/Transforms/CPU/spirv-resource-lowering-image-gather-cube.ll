; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Covers roadmap H124r: `spv_resource_gather`/`spv_resource_gather_cmp`
; against a `Cube`-shaped handle (HLSL's `TextureCube::Gather{,Red,Green,
; Blue,Alpha}()`/`GatherCmp()`, confirmed via a real `check-hlsl-feme-vk`
; offload-test-suite run of `Vk.Gather.test.yaml`/`GatherCmp.test.yaml`)
; lower to `feme.cpu.image.gather.cube.v4f32`/
; `feme.cpu.image.gathercmp.cube.v4f32`. Mirrors
; spirv-resource-lowering-image-gather-array2d.ll's own `Array2D`
; siblings, except the coordinate is `Cube`'s own 3-component direction
; vector (mirroring `SampleCube`'s identical convention) rather than
; `Array2D`'s `(U, V, ArrayLayer)` shape, and there is no offset operand
; at all: SPIR-V forbids `ConstOffset` against `Dim::Cube` outright.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <4 x float> @gathercmp_cube(
define <4 x float> @gathercmp_cube(<3 x float> %dir, float %dref) {
  %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[DX:.*]] = extractelement <3 x float> %dir, i64 0
  ; CHECK: %[[DY:.*]] = extractelement <3 x float> %dir, i64 1
  ; CHECK: %[[DZ:.*]] = extractelement <3 x float> %dir, i64 2
  ; CHECK: call <4 x float> @feme.cpu.image.gathercmp.cube.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[DX]], float %[[DY]], float %[[DZ]], float %dref, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather.cmp(
      target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %dir, float %dref,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; CHECK-LABEL: define <4 x float> @gather_cube(
define <4 x float> @gather_cube(<3 x float> %dir, i32 %component) {
  %img = call target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg.cube(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp.cube(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: %[[DX:.*]] = extractelement <3 x float> %dir, i64 0
  ; CHECK: %[[DY:.*]] = extractelement <3 x float> %dir, i64 1
  ; CHECK: %[[DZ:.*]] = extractelement <3 x float> %dir, i64 2
  ; CHECK: call <4 x float> @feme.cpu.image.gather.cube.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %[[DX]], float %[[DY]], float %[[DZ]], i32 %component, i1 true)
  %r = call <4 x float> @llvm.spv.resource.gather(
      target("spirv.Image", float, 3, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %dir, i32 %component,
      <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 3, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg.cube(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp.cube(i32, i32, i32, i32, ptr)
