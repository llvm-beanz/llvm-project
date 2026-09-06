; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L64: an arrayed explicit-`Grad` sample. SPIR-V gives a `Grad`
; derivative one component per image dimension *not counting* the array
; layer (a layer index selects a discrete slice rather than addressing a
; filtered axis, so it has no derivative at all), so `Array2D` pairs a
; 3-component `(U, V, Layer)` coordinate with 2-component derivatives, and
; `CubeArray` a 4-component `(X, Y, Z, Layer)` coordinate with 3-component
; ones.
;
; `hasOnlySupportedImageUses` used to require both derivatives to be as
; wide as the coordinate itself, which is only correct for the two
; non-arrayed shapes. Every real arrayed `textureGrad()` shader therefore
; had its sample rejected -- and, because an unsupported *use* makes the
; whole resource handle un-normalizable, the entire function was left
; unlowered, so `checkSupportedRaisedOps` went on to report whichever
; `handlefrombinding` declaration happened to come first in the module.
; That was usually an innocent uniform-block `spirv.VulkanBuffer` handle,
; which is why this bug was long mis-filed as a `VulkanBuffer` gap.
;
; Both functions below are reduced directly from the real IR of
; dEQP-VK.glsl.texture_functions.texturegrad.sampler2darray_fixed_fragment,
; including the two scale/bias uniform blocks that were being misreported,
; so this file pins the fix and the misreporting together.

target triple = "spirv-unknown-vulkan-compute"

; Texture2DArray: [Dim=2D(1), Depth=0, Arrayed=1, MS=0, Sampled=1, Format=0].
; The (U, V) derivatives land in the DUdX/DUdY/DVdX/DVdY operand slots, and
; the array layer is passed as an ordinary coordinate with no derivative of
; its own.
; CHECK-LABEL: define <4 x float> @samplegrad_array2d(
; CHECK: %[[DUDX:.*]] = extractelement <2 x float> %dpdx, i64 0
; CHECK: %[[DUDY:.*]] = extractelement <2 x float> %dpdy, i64 0
; CHECK: %[[DVDX:.*]] = extractelement <2 x float> %dpdx, i64 1
; CHECK: %[[DVDY:.*]] = extractelement <2 x float> %dpdy, i64 1
; CHECK: call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: float %[[DUDX]], float %[[DUDY]], float %[[DVDX]], float %[[DVDY]]
define <4 x float> @samplegrad_array2d(<3 x float> %coord, <2 x float> %dpdx,
                                       <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg2darray(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg2darray(
      target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord,
      <2 x float> %dpdx, <2 x float> %dpdy, <3 x i32> zeroinitializer)
  ret <4 x float> %r
}

; TextureCubeArray: [Dim=Cube(3), Depth=0, Arrayed=1, MS=0, Sampled=1].
; Three direction-vector derivative components, one per cube axis; the
; trailing array-layer coordinate component again has none.
; CHECK-LABEL: define <4 x float> @samplegrad_cubearray(
; CHECK: extractelement <3 x float> %dpdx, i64 2
; CHECK: extractelement <3 x float> %dpdy, i64 2
; CHECK: call <4 x float> @feme.cpu.image.sample.cubearray.v4f32(
define <4 x float> @samplegrad_cubearray(<4 x float> %coord, <3 x float> %dpdx,
                                         <3 x float> %dpdy) {
  %img = call target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timgcubearray(i32 0, i32 2, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 3, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.samplegrad.v4f32.timgcubearray(
      target("spirv.Image", float, 3, 0, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <4 x float> %coord,
      <3 x float> %dpdx, <3 x float> %dpdy, <3 x i32> zeroinitializer)
  ret <4 x float> %r
}

; The uniform-block handle that used to be misreported as the cause. It
; lowers to an ordinary constant-buffer load once the sample above no
; longer poisons the whole function.
; CHECK-LABEL: define <4 x float> @samplegrad_array2d_with_uniform_scale(
; CHECK-NOT: llvm.spv.resource
; CHECK: call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
define <4 x float> @samplegrad_array2d_with_uniform_scale(<3 x float> %coord,
                                                          <2 x float> %dpdx,
                                                          <2 x float> %dpdy) {
  %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg2darray(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg2darray(
      target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord,
      <2 x float> %dpdx, <2 x float> %dpdy, <3 x i32> zeroinitializer)
  %ubo = call target("spirv.VulkanBuffer", { <4 x float> }, 2, 0)
      @llvm.spv.resource.handlefrombinding.tubo(i32 0, i32 4, i32 1, i32 0, ptr null)
  %ptr = call ptr addrspace(12)
      @llvm.spv.resource.getpointer.p12.tubo(target("spirv.VulkanBuffer", { <4 x float> }, 2, 0) %ubo, i32 0)
  %scale = load <4 x float>, ptr addrspace(12) %ptr, align 4
  %scaled = fmul <4 x float> %r, %scale
  ret <4 x float> %scaled
}

declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg2darray(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 3, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timgcubearray(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare target("spirv.VulkanBuffer", { <4 x float> }, 2, 0)
    @llvm.spv.resource.handlefrombinding.tubo(i32, i32, i32, i32, ptr)
declare ptr addrspace(12)
    @llvm.spv.resource.getpointer.p12.tubo(target("spirv.VulkanBuffer", { <4 x float> }, 2, 0), i32)
declare <4 x float> @llvm.spv.resource.samplegrad.v4f32.timg2darray(
    target("spirv.Image", float, 1, 0, 1, 0, 1, 0), target("spirv.Sampler"),
    <3 x float>, <2 x float>, <2 x float>, <3 x i32>)
declare <4 x float> @llvm.spv.resource.samplegrad.v4f32.timgcubearray(
    target("spirv.Image", float, 3, 0, 1, 0, 1, 0), target("spirv.Sampler"),
    <4 x float>, <3 x float>, <3 x float>, <3 x i32>)
