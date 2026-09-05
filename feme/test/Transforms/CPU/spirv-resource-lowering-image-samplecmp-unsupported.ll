; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L46 narrowed depth-comparison sample support to `Plain2D`, a
; zero `ConstOffset`, and no `MinLod` clamp (see
; `isDrefSampleIntrinsic`'s comment in SPIRVResourceLowering.cpp, filed as
; roadmap L48): each function below hits exactly one of those narrowings
; and so is left entirely unrewritten, matching
; spirv-resource-lowering-unsupported.ll's own "left untouched" contract.

target triple = "spirv-unknown-vulkan-compute"

; `samplecmp_clamp`'s own trailing `MinLod` clamp operand has no
; `createSampleCmp2D` counterpart yet.
; CHECK-LABEL: define float @samplecmp_clamp_unsupported(
; CHECK-NOT: feme.cpu.image
; CHECK: call float @llvm.spv.resource.samplecmp.clamp
define float @samplecmp_clamp_unsupported(<3 x float> %coord, float %dref, float %clamp) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call float @llvm.spv.resource.samplecmp.clamp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> zeroinitializer, float %clamp)
  ret float %r
}

; A nonzero `ConstOffset` -- `createSampleCmp2D` has no offset operand at
; all, unlike `createSample2D`'s own roadmap L26 support.
; CHECK-LABEL: define float @samplecmp_offset_unsupported(
; CHECK-NOT: feme.cpu.image
; CHECK: call float @llvm.spv.resource.samplecmp
define float @samplecmp_offset_unsupported(<3 x float> %coord, float %dref) {
  %img = call target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %off0 = insertelement <3 x i32> poison, i32 1, i32 0
  %off1 = insertelement <3 x i32> %off0, i32 0, i32 1
  %off = insertelement <3 x i32> %off1, i32 0, i32 2
  %r = call float @llvm.spv.resource.samplecmp(
      target("spirv.Image", float, 1, 2, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, float %dref,
      <3 x i32> %off)
  ret float %r
}

declare target("spirv.Image", float, 1, 2, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
