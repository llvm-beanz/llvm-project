; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L73: `OpImageQuerySamples` (GLSL's `textureSamples(sampler2DMS)`)
; -- like `OpImageQuerySizeLod`/`OpImageQueryLevels` (roadmap L72(d)), this
; opcode has no real MLIR/LLVM intrinsic of its own, so `SPIRVImporter.cpp`'s
; own `lowerImageQueryOpcodes` rewrites it into an ordinary call against a
; synthesized, magic-named external function (`feme.query.samples.*`)
; before MLIR ever sees the original opcode -- `SPIRVResourceLoweringPass`
; recognizes a call to this magic name by its callee's own name prefix
; (`isQuerySamplesCall`). Unlike `OpImageQuerySizeLod`/`OpImageQueryLevels`
; (`Plain2D` only), this opcode is spec-legal only against a multisampled
; image, so `classifySampledImage2DHandle` widens its own multisample
; rejection to accept a 2D multisampled sampled image (`Plain2DMS`/
; `Array2DMS`) specifically for this call.

target triple = "spirv-unknown-vulkan-compute"

; A plain 2D multisampled sampled image's own `textureSamples(sampler)`
; query.

; CHECK-LABEL: define i32 @sampled_image_2dms_query_samples(
define i32 @sampled_image_2dms_query_samples() {
  %img = call target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call i32 @feme.cpu.image.querysamples.i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}})
  %samples = call i32 @"feme.query.samples.0"(
      target("spirv.Image", float, 1, 0, 0, 1, 1, 0) %img)
  ret i32 %samples
}

; The arrayed counterpart (`Array2DMS`) -- confirms `Arrayed` is threaded
; correctly through the same multisample-widening path.

; CHECK-LABEL: define i32 @sampled_image_array2dms_query_samples(
define i32 @sampled_image_array2dms_query_samples() {
  %img = call target("spirv.Image", float, 1, 0, 1, 1, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg2(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call i32 @feme.cpu.image.querysamples.i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}})
  %samples = call i32 @"feme.query.samples.1"(
      target("spirv.Image", float, 1, 0, 1, 1, 1, 0) %img)
  ret i32 %samples
}

declare target("spirv.Image", float, 1, 0, 0, 1, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare i32 @"feme.query.samples.0"(
    target("spirv.Image", float, 1, 0, 0, 1, 1, 0))

declare target("spirv.Image", float, 1, 0, 1, 1, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg2(i32, i32, i32, i32, ptr)
declare i32 @"feme.query.samples.1"(
    target("spirv.Image", float, 1, 0, 1, 1, 1, 0))
