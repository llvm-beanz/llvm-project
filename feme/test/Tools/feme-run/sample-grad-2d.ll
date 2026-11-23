; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Five lanes exercise scalar/vector results, signed offsets, clamps, partial
; waves and guarded repeats. Multi-mip LOD selection is covered by compiled
; stage tests; the runner's image YAML currently prepares one mip only.
; CHECK: heap[1]: 42 5 10 7 8 5 10 7 8 5 10 7 8 5 10 7 8 5 10 7 8 42
;--- shader.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 1)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %bit = and i32 %tid, 1
  %odd = icmp ne i32 %bit, 0
  %u = select i1 %odd, float 0.75, float 0.25
  %g = select i1 %odd, float 1.0, float 0.25
  %clamp = select i1 %odd, float 0.0, float 2.0
  %c0 = insertelement <2 x float> poison, float %u, i32 0
  %coord = insertelement <2 x float> %c0, float 0.75, i32 1
  %dx = insertelement <2 x float> zeroinitializer, float %g, i32 0
  %dy = insertelement <2 x float> zeroinitializer, float %g, i32 1
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
  %rg = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 -1, i32 1>, float %clamp)
  %rgb = call <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
  %rgba = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, <2 x float> %dx, <2 x float> %dy, <2 x i32> <i32 1, i32 -1>)
  %g0 = extractelement <2 x float> %rg, i32 1
  %b = extractelement <3 x float> %rgb, i32 2
  %a = extractelement <4 x float> %rgba, i32 3
  %ri = fptoui float %r to i32
  %gi = fptoui float %g0 to i32
  %bi = fptoui float %b to i32
  %ai = fptoui float %a to i32
  %base0 = mul i32 %tid, 16
  %base = add i32 %base0, 4
  %o1 = add i32 %base, 4
  %o2 = add i32 %base, 8
  %o3 = add i32 %base, 12
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %ri)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %o1, i32 poison, i32 %gi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %o2, i32 poison, i32 %bi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %o3, i32 poison, i32 %ai)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>, float)
declare <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2f32.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x float>, <2 x float>, <2 x i32>)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;--- heap.yaml
resource-heap:
  - index: 1
    size: 88
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: 2d
    extent: [2, 2]
    format: r32g32b32a32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432, 1084227584, 1086324736, 1088421888, 1090519040, 1091567616, 1092616192, 1093664768, 1094713344, 1095761920, 1096810496, 1097859072, 1098907648]
samplers:
  - index: 0
    address-u: clamp-to-edge
    address-v: clamp-to-edge
