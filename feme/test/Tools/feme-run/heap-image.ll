; RUN: split-file %s %t
; RUN: feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; Roadmap R31, "heap YAML image resource class" (see feme/docs/Roadmap.md's
; §2.6.1): each lane loads its own texel (mip 0, `(tid, 0)`) out of a 4x1
; `r32g32b32a32_float` image built from the new `images` heap YAML entry,
; and copies it into a raw buffer so the result is observable. `--reference`
; is used because per-lane divergent image addressing does not yet SIMD-
; widen (roadmap R30's own still-open item -- see "Canonical image
; operations" in feme/docs/FeMeGraphicsDesign.md); this test's own contract
; is the image heap entry itself, not SIMD widening.

; Each lane copies its own texel's four float components verbatim.
; CHECK: heap[1]: 1065353216 1073741824 1077936128 1082130432 1073741824 1082130432 1086324736 1090519040 1077936128 1086324736 1091567616 1094713344 1082130432 1090519040 1094713344 1097859072
; CHECK: image[0]: 1065353216 1073741824 1077936128 1082130432 1073741824 1082130432 1086324736 1090519040 1077936128 1086324736 1091567616 1094713344 1082130432 1090519040 1094713344 1097859072

;--- shader.ll
define void @main() #0 {
  %tex = call target("dx.Texture", <4 x float>, 0, 0, 0, 2)
      @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0, i1 false)
  %out = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 1, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %coord0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 0, i32 1
  %texel = call <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_v4f32_0_0_0_2t.v2i32.i32.v2i32(
      target("dx.Texture", <4 x float>, 0, 0, 0, 2) %tex, <2 x i32> %coord, i32 0, <2 x i32> zeroinitializer)
  %r = extractelement <4 x float> %texel, i32 0
  %g = extractelement <4 x float> %texel, i32 1
  %b = extractelement <4 x float> %texel, i32 2
  %a = extractelement <4 x float> %texel, i32 3
  %ri = bitcast float %r to i32
  %gi = bitcast float %g to i32
  %bi = bitcast float %b to i32
  %ai = bitcast float %a to i32
  %base = mul i32 %tid, 16
  %off1 = add i32 %base, 4
  %off2 = add i32 %base, 8
  %off3 = add i32 %base, 12
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %ri)
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %out, i32 %off1, i32 poison, i32 %gi)
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %out, i32 %off2, i32 poison, i32 %bi)
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %out, i32 %off3, i32 poison, i32 %ai)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2)
    @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32, i1)
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32, i1)
declare <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_v4f32_0_0_0_2t.v2i32.i32.v2i32(
    target("dx.Texture", <4 x float>, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

;--- heap.yaml
resource-heap:
  - index: 1
    size: 64
images:
  - index: 0
    dimension: 2d
    extent: [4, 1]
    format: r32g32b32a32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432,
           1073741824, 1082130432, 1086324736, 1090519040,
           1077936128, 1086324736, 1091567616, 1094713344,
           1082130432, 1090519040, 1094713344, 1097859072]
