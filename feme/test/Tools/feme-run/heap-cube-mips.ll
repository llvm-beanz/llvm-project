; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --wave-size=8 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Zero gradients plus a divergent minimum LOD distinguish mip-major cube
; faces and the second cube's +Z face, including a linear half-mip blend.
; CHECK: heap[2]: 42 1 2 6 7 11 12 11 12 11 12 42
;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %f = uitofp i32 %tid to float
  %lod = fmul float %f, 0.5
  %t = call target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32 0)
  %a = call target("dx.Texture", float, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_9t(i32 1)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %r = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> <float 1.0, float 0.0, float 0.0>, <3 x float> zeroinitializer, <3 x float> zeroinitializer, i32 0, float %lod)
  %ar = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> <float 0.0, float 0.0, float 1.0, float 1.0>, <3 x float> zeroinitializer, <3 x float> zeroinitializer, i32 0, float %lod)
  %ri = fptoui float %r to i32
  %ai = fptoui float %ar to i32
  %base0 = mul i32 %tid, 8
  %base = add i32 %base0, 4
  %next = add i32 %base, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %ri)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %next, i32 poison, i32 %ai)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32)
declare target("dx.Texture", float, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_9t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32, float)
declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;--- heap.yaml
resource-heap:
  - {index: 2, size: 48, data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]}
images:
  - index: 0
    dimension: cube
    extent: [2, 2]
    mip-levels: 2
    format: r32_float
    data: [1065353216, 1065353216, 1065353216, 1065353216,
           1073741824, 1073741824, 1073741824, 1073741824,
           1077936128, 1077936128, 1077936128, 1077936128,
           1082130432, 1082130432, 1082130432, 1082130432,
           1084227584, 1084227584, 1084227584, 1084227584,
           1086324736, 1086324736, 1086324736, 1086324736,
           1093664768, 1094713344, 1095761920, 1096810496, 1097859072, 1098907648]
  - index: 1
    dimension: cube-array
    array-layers: 12
    extent: [2, 2]
    mip-levels: 2
    format: r32_float
    data: [1065353216, 1065353216, 1065353216, 1065353216,
           1073741824, 1073741824, 1073741824, 1073741824,
           1077936128, 1077936128, 1077936128, 1077936128,
           1082130432, 1082130432, 1082130432, 1082130432,
           1084227584, 1084227584, 1084227584, 1084227584,
           1086324736, 1086324736, 1086324736, 1086324736,
           1086324736, 1086324736, 1086324736, 1086324736,
           1084227584, 1084227584, 1084227584, 1084227584,
           1082130432, 1082130432, 1082130432, 1082130432,
           1077936128, 1077936128, 1077936128, 1077936128,
           1073741824, 1073741824, 1073741824, 1073741824,
           1065353216, 1065353216, 1065353216, 1065353216,
           1093664768, 1094713344, 1095761920, 1096810496, 1097859072, 1098907648,
           1098907648, 1097859072, 1096810496, 1095761920, 1094713344, 1093664768]
samplers:
  - {index: 0, min-filter: nearest, mag-filter: nearest, mip-filter: linear, address-u: clamp-to-edge, address-v: clamp-to-edge, address-w: clamp-to-edge}
