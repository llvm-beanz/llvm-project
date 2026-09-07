; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L66(e): a real use-after-free, found via CTS re-runs once L66's
; other sub-items began clearing more pipelines. `@sample_2d` and
; `@sample_3d` each declare an *image* handle at the same (set 0, binding 0)
; identity but with two different shapes (`Dim2D` vs. `Dim3D`) -- a
; conflicting re-declaration, mirroring
; `spirv-resource-lowering-conflicting.ll`'s own buffer-shaped precedent --
; while both functions' own *sampler* handle shares one single,
; non-conflicting (set 0, binding 1) identity. Before this fix,
; `lowerImageAccesses`'s own trailing cleanup loop unconditionally erased
; every handle it was given, including an accepted sampler handle whose
; paired (excluded, conflicting) image handle meant the sample call through
; it was deliberately left unrewritten and still live -- referencing that
; sampler handle as an operand. `Instruction::eraseFromParent` on a
; still-used `Value` aborts an assertions-enabled build
; (`Uses remain when a value is destroyed!`); this file simply not
; crashing `feme-opt` at all is the real regression test here, alongside
; the same explicit "left unlowered" `CHECK`s
; `spirv-resource-lowering-conflicting.ll` already uses for a conflicting
; buffer identity.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define <4 x float> @sample_2d(
; CHECK: @llvm.spv.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
; CHECK: @llvm.spv.resource.sample.v4f32.{{.*}}(
; CHECK-NOT: call <4 x float> @feme.cpu.image.sample.2d.v4f32(
define <4 x float> @sample_2d(<2 x float> %coord) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg2d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg2d(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <2 x float> %coord, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}

; CHECK-LABEL: define <4 x float> @sample_3d(
; CHECK: @llvm.spv.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
; CHECK: @llvm.spv.resource.sample.v4f32.{{.*}}(
; CHECK-NOT: call <4 x float> @feme.cpu.image.sample.3d.v4f32(
define <4 x float> @sample_3d(<3 x float> %coord) {
  %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %samp = call target("spirv.Sampler")
      @llvm.spv.resource.handlefrombinding.tsamp(i32 0, i32 1, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
      target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img,
      target("spirv.Sampler") %samp, <3 x float> %coord, <3 x i32> zeroinitializer)
  ret <4 x float> %r
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg2d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
declare target("spirv.Sampler")
    @llvm.spv.resource.handlefrombinding.tsamp(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.sample.v4f32.timg2d(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0), target("spirv.Sampler"),
    <2 x float>, <2 x i32>)
declare <4 x float> @llvm.spv.resource.sample.v4f32.timg3d(
    target("spirv.Image", float, 2, 0, 0, 0, 1, 0), target("spirv.Sampler"),
    <3 x float>, <3 x i32>)
