; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Canonical execution covers all faces and two cube-array elements without
; requiring SDK DXC. Unused poison offsets never reach sampling helpers.
; CHECK: heap[2]: 42 0 0 0 0 0 0 0 0 1 1 1 1 0 1 0 1 0 0 0 0 1 0 1 0 0 0 42
;
;--- shader.ll
@directions = private constant [6 x <3 x float>] [<3 x float> <float 1.0, float 0.0, float 0.0>, <3 x float> <float -1.0, float 0.0, float 0.0>, <3 x float> <float 0.0, float 1.0, float 0.0>, <3 x float> <float 0.0, float -1.0, float 0.0>, <3 x float> <float 0.0, float 0.0, float 1.0>, <3 x float> <float 0.0, float 0.0, float -1.0>]
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 0)
  %a = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 1)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %face = urem i32 %tid, 6
  %ptr = getelementptr [6 x <3 x float>], ptr @directions, i32 0, i32 %face
  %dir = load <3 x float>, ptr %ptr
  %cube = udiv i32 %tid, 6
  %layer = and i32 %cube, 1
  %lf = uitofp i32 %layer to float
  %wide = shufflevector <3 x float> %dir, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
  %coord = insertelement <4 x float> %wide, float %lf, i32 3
  %p = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %dir, float 0.5, i32 poison)
  %pa = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %a, target("dx.Sampler", 1) %s, <4 x float> %coord, float 0.5, i32 poison)
  %pi = fptoui float %p to i32
  %pai = fptoui float %pa to i32
  %b0 = mul i32 %tid, 8
  %base = add i32 %b0, 4
  %b1 = add i32 %base, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %pi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %pai)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="13,1,1" }
;
;--- heap.yaml
resource-heap:
  - index: 2
    size: 112
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
           42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: cube
    extent: [1, 1]
    format: r32_float
    data: [1040187392, 1048576000, 1052770304, 1056964608, 1059061760, 1061158912]
  - index: 1
    dimension: cube-array
    array-layers: 12
    extent: [1, 1]
    format: r32_float
    data: [1040187392, 1048576000, 1052770304, 1056964608, 1059061760, 1061158912,
           1061158912, 1059061760, 1056964608, 1052770304, 1048576000, 1040187392]
samplers:
  - index: 0
    compare-enable: true
    compare-func: less
