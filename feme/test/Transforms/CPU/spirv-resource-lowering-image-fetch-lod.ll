; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L72: GLSL's `texelFetch(sampler2D, ivec2, int)` -- unlike a
; zero-mip-only fetch (`spirv-resource-lowering-image.ll`'s own `@fetch`,
; raised through `getpointer` + `load`, see `feme::spirv::ImageFetchPattern`)
; -- always supplies an explicit LOD, which SPIR-V's own `OpImageFetch`
; carries as a `Lod` image operand; `feme::spirv::ImageFetchLodPattern`
; raises that shape to `llvm.spv.resource.load.level` instead of the
; `getpointer` shape, a distinct intrinsic `hasOnlySupportedImageUses`
; previously never recognized at all (rejecting every real `texelFetch()`
; call against a sampled image outright). \p Offset (the intrinsic's
; fourth operand) may be a real, nonzero `ConstOffset` (roadmap L72(b)'s
; own `texelFetchOffset()` support, folded into the coordinate itself
; before the runtime call rather than threaded through as a separate
; argument); roadmap L72(c) further widened the shape gate itself beyond
; `Plain2D`/`Array2D` to also cover `Plain1D`/`Array1D`/`Plain3D`.

target triple = "spirv-unknown-vulkan-compute"

; A plain (non-arrayed) 2D fetch: the real, caller-supplied LOD threads
; through as `feme.cpu.image.load.2d.v4f32`'s own `Mip` operand, rather
; than the hardcoded zero the `getpointer`-based fetch path always passes.

; CHECK-LABEL: define <4 x float> @fetch_level(
; CHECK-SAME: <2 x i32> %coord, i32 %lod, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define <4 x float> @fetch_level(<2 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; Even a compile-time-zero `ConstOffset` still folds through a real
  ; `add` instruction (roadmap L72(b)'s own fold is unconditional, not
  ; gated on whether the offset happens to be zero), so `[[X]]`/`[[Y]]`
  ; bind to the `add`'s own result, not the bare `extractelement`.
  ; CHECK: %[[X0:.*]] = extractelement <2 x i32> %coord, i64 0
  ; CHECK: %[[X:.*]] = add i32 %[[X0]], 0
  ; CHECK: %[[Y0:.*]] = extractelement <2 x i32> %coord, i64 1
  ; CHECK: %[[Y:.*]] = add i32 %[[Y0]], 0
  ; CHECK: call <4 x float> @feme.cpu.image.load.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %[[X]], i32 %[[Y]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timg.v2i32.i32.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
      i32 %lod, <2 x i32> zeroinitializer)
  ret <4 x float> %v
}

; An integer-channel image fetch dispatches to the integer entry point
; instead (roadmap E26's own existing distinction, now threaded through
; this new shape too).

; CHECK-LABEL: define <4 x i32> @fetch_level_i32(
define <4 x i32> @fetch_level_i32(<2 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timgi32(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call <4 x i32> @feme.cpu.image.load.2d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %{{.*}}, i32 %{{.*}}, i32 %lod, i32 0, i1 true)
  %v = call <4 x i32> @llvm.spv.resource.load.level.v4i32.timgi32.v2i32.i32.v2i32(
      target("spirv.Image", i32, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
      i32 %lod, <2 x i32> zeroinitializer)
  ret <4 x i32> %v
}

; `Array2D` (roadmap H7b-a): the coordinate's 3rd component is the array
; layer, extracted and threaded through as `Load2DArray`'s own `Layer`
; operand alongside the real `Mip`.

; CHECK-LABEL: define <4 x float> @fetch_level_array(
define <4 x float> @fetch_level_array(<3 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timgarr(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: %[[X0:.*]] = extractelement <3 x i32> %coord, i64 0
  ; CHECK: %[[X:.*]] = add i32 %[[X0]], 0
  ; CHECK: %[[Y0:.*]] = extractelement <3 x i32> %coord, i64 1
  ; CHECK: %[[Y:.*]] = add i32 %[[Y0]], 0
  ; CHECK: %[[L:.*]] = extractelement <3 x i32> %coord, i64 2
  ; CHECK: call <4 x float> @feme.cpu.image.load.2darray.v4f32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 %[[X]], i32 %[[Y]], i32 %[[L]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timgarr.v3i32.i32.v3i32(
      target("spirv.Image", float, 1, 0, 1, 0, 1, 0) %img, <3 x i32> %coord,
      i32 %lod, <3 x i32> zeroinitializer)
  ret <4 x float> %v
}

; roadmap L72(b): a real, nonzero `ConstOffset` folds into the coordinate
; itself (an `add` per component) rather than threading through as a
; separate runtime-call argument.

; CHECK-LABEL: define <4 x float> @fetch_level_offset(
define <4 x float> @fetch_level_offset(<2 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: %[[X0:.*]] = extractelement <2 x i32> %coord, i64 0
  ; CHECK: %[[X:.*]] = add i32 %[[X0]], 1
  ; CHECK: %[[Y0:.*]] = extractelement <2 x i32> %coord, i64 1
  ; CHECK: %[[Y:.*]] = add i32 %[[Y0]], -2
  ; CHECK: call <4 x float> @feme.cpu.image.load.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %[[X]], i32 %[[Y]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timg.v2i32.i32.v2i32(
      target("spirv.Image", float, 1, 0, 0, 0, 1, 0) %img, <2 x i32> %coord,
      i32 %lod, <2 x i32> <i32 1, i32 -2>)
  ret <4 x float> %v
}

; roadmap L72(c): `Plain1D`. The lone coordinate/offset are both bare
; scalars, not a 1-element vector, so no `extractelement` is emitted at
; all.

; CHECK-LABEL: define <4 x float> @fetch_level_1d(
define <4 x float> @fetch_level_1d(i32 %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: %[[X:.*]] = add i32 %coord, 5
  ; CHECK: call <4 x float> @feme.cpu.image.load.1d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 3, i32 %[[X]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timg1d.i32.i32.i32(
      target("spirv.Image", float, 0, 0, 0, 0, 1, 0) %img, i32 %coord, i32 %lod,
      i32 5)
  ret <4 x float> %v
}

; roadmap L72(c): `Array1D`. The 2-wide coordinate's 2nd component is the
; array layer, threaded through as `Load1DArray`'s own `Layer` operand
; untouched by any real offset (mirroring `Array2D`'s own precedent above).

; CHECK-LABEL: define <4 x float> @fetch_level_1d_array(
define <4 x float> @fetch_level_1d_array(<2 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg1darr(i32 0, i32 4, i32 1, i32 0, ptr null)
  ; CHECK: %[[X0:.*]] = extractelement <2 x i32> %coord, i64 0
  ; CHECK: %[[X:.*]] = add i32 %[[X0]], 0
  ; CHECK: %[[L:.*]] = extractelement <2 x i32> %coord, i64 1
  ; CHECK: call <4 x float> @feme.cpu.image.load.1darray.v4f32(ptr %image_heap, i32 %image_heap_count, i32 4, i32 %[[X]], i32 %[[L]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timg1darr.v2i32.i32.i32(
      target("spirv.Image", float, 0, 0, 1, 0, 1, 0) %img, <2 x i32> %coord,
      i32 %lod, i32 0)
  ret <4 x float> %v
}

; roadmap L72(c): `Plain3D`. The 3-wide coordinate's own `X`/`Y`/`Z`
; components each fold in the matching component of a real `ConstOffset`.

; CHECK-LABEL: define <4 x float> @fetch_level_3d(
define <4 x float> @fetch_level_3d(<3 x i32> %coord, i32 %lod) {
  %img = call target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
      @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: %[[X0:.*]] = extractelement <3 x i32> %coord, i64 0
  ; CHECK: %[[X:.*]] = add i32 %[[X0]], 1
  ; CHECK: %[[Y0:.*]] = extractelement <3 x i32> %coord, i64 1
  ; CHECK: %[[Y:.*]] = add i32 %[[Y0]], -2
  ; CHECK: %[[Z0:.*]] = extractelement <3 x i32> %coord, i64 2
  ; CHECK: %[[Z:.*]] = add i32 %[[Z0]], 3
  ; CHECK: call <4 x float> @feme.cpu.image.load.3d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 5, i32 %[[X]], i32 %[[Y]], i32 %[[Z]], i32 %lod, i32 0, i1 true)
  %v = call <4 x float> @llvm.spv.resource.load.level.v4f32.timg3d.v3i32.i32.v3i32(
      target("spirv.Image", float, 2, 0, 0, 0, 1, 0) %img, <3 x i32> %coord,
      i32 %lod, <3 x i32> <i32 1, i32 -2, i32 3>)
  ret <4 x float> %v
}

declare target("spirv.Image", float, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare target("spirv.Image", i32, 1, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timgi32(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 1, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timgarr(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 0, 0, 1, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg1darr(i32, i32, i32, i32, ptr)
declare target("spirv.Image", float, 2, 0, 0, 0, 1, 0)
    @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.timg.v2i32.i32.v2i32(
    target("spirv.Image", float, 1, 0, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
declare <4 x i32> @llvm.spv.resource.load.level.v4i32.timgi32.v2i32.i32.v2i32(
    target("spirv.Image", i32, 1, 0, 0, 0, 1, 0), <2 x i32>, i32, <2 x i32>)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.timgarr.v3i32.i32.v3i32(
    target("spirv.Image", float, 1, 0, 1, 0, 1, 0), <3 x i32>, i32, <3 x i32>)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.timg1d.i32.i32.i32(
    target("spirv.Image", float, 0, 0, 0, 0, 1, 0), i32, i32, i32)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.timg1darr.v2i32.i32.i32(
    target("spirv.Image", float, 0, 0, 1, 0, 1, 0), <2 x i32>, i32, i32)
declare <4 x float> @llvm.spv.resource.load.level.v4f32.timg3d.v3i32.i32.v3i32(
    target("spirv.Image", float, 2, 0, 0, 0, 1, 0), <3 x i32>, i32, <3 x i32>)
