; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L72(d): `OpImageQuerySizeLod` (GLSL's `textureSize(sampler, lod)`)
; and `OpImageQueryLevels` (GLSL's `textureQueryLevels(sampler)`) -- neither
; opcode has a real MLIR/LLVM intrinsic of its own (MLIR's SPIR-V dialect
; has zero enum coverage for either), so `SPIRVImporter.cpp`'s own
; `lowerImageQueryOpcodes` rewrites each into an ordinary call against a
; synthesized, magic-named external function (`feme.query.size_lod.*`/
; `feme.query.levels.*`) before MLIR ever sees the original opcode --
; `SPIRVResourceLoweringPass` recognizes a call to one of these magic names
; by its callee's own name prefix (`isQuerySizeLodCall`/
; `isQueryLevelsCall`), the same way it already recognizes an
; `llvm.spv.resource.*` intrinsic call.

target triple = "spirv-unknown-vulkan-compute"

; A plain 2D sampled image's own `textureSize(sampler, lod)` query.

; CHECK-LABEL: define <2 x i32> @sampled_image_size_lod(
define <2 x i32> @sampled_image_size_lod(i32 %lod) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call <2 x i32> @feme.cpu.image.getdimensions.lod.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i32 %lod, i1 true)
  %dims = call <2 x i32> @"feme.query.size_lod.0"(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, i32 %lod)
  ret <2 x i32> %dims
}

; A plain 2D sampled image's own `textureQueryLevels(sampler)` query,
; alongside an ordinary `textureSize()` (`OpImageQuerySize`, no explicit
; LOD) query on that same handle -- confirms this handle's other,
; already-supported use is not rejected merely because `OpImageQueryLevels`
; is also present.

; CHECK-LABEL: define i32 @sampled_image_query_levels(
define i32 @sampled_image_query_levels() {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg2(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK-DAG: call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}})
  %levels = call i32 @"feme.query.levels.0"(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img)
  ; CHECK-DAG: call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i1 true)
  %dims = call <2 x i32> @llvm.spv.resource.getdimensions.xy.timg2(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img)
  %width = extractelement <2 x i32> %dims, i32 0
  %result = add i32 %levels, %width
  ret i32 %result
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare <2 x i32> @"feme.query.size_lod.0"(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0), i32)

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg2(i32, i32, i32, i32, ptr)
declare i32 @"feme.query.levels.0"(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0))
declare <2 x i32> @llvm.spv.resource.getdimensions.xy.timg2(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0))

; Roadmap L75: an `Array2D` sampled image's own `textureSize(sampler, lod)`
; query -- widened past L72(d)'s original `Plain2D`-only scope, dispatching
; to the dedicated `QuerySizeLod2DArray` builder (v3i32 result: the extra
; third lane is the real, unscaled array-layer count).

; CHECK-LABEL: define <3 x i32> @sampled_array2d_image_size_lod(
define <3 x i32> @sampled_array2d_image_size_lod(i32 %lod) {
  %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg3(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 {{[0-9]+}}, i32 %lod, i1 true)
  %dims = call <3 x i32> @"feme.query.size_lod.1"(
      target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, i32 %lod)
  ret <3 x i32> %dims
}

declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg3(i32, i32, i32, i32, ptr)
declare <3 x i32> @"feme.query.size_lod.1"(
    target("spirv.Image", float, 1, 0, 1, 0, 1, 0), i32)
