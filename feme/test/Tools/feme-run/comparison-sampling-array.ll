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
; Two layers have reversed depth values. Offsets affect only U/V, while
; point and bilinear comparison keep each lane's selected array layer.
; CHECK: heap[1]: 42 0 2 0 4 2 0 4 2 4 0 2 4 0 2 0 42
;
;--- shader.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %linear = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 1)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 1)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %column = and i32 %tid, 1
  %shift = lshr i32 %tid, 1
  %layer = and i32 %shift, 1
  %lf = uitofp i32 %layer to float
  %cf = uitofp i32 %column to float
  %scaled = fmul float %cf, 0.5
  %u = fadd float %scaled, 0.25
  %c0 = insertelement <3 x float> poison, float %u, i32 0
  %c1 = insertelement <3 x float> %c0, float 0.5, i32 1
  %coord = insertelement <3 x float> %c1, float %lf, i32 2
  %center = insertelement <3 x float> <float 0.5, float 0.5, float poison>, float %lf, i32 2
  %right = insertelement <3 x float> <float 0.75, float 0.5, float poison>, float %lf, i32 2
  %point = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
  %pcf = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %linear, <3 x float> %center, float 0.5, <2 x i32> zeroinitializer)
  %offset = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %right, float 0.5, <2 x i32> <i32 -1, i32 0>)
  %p4 = fmul float %point, 4.0
  %f4 = fmul float %pcf, 4.0
  %o4 = fmul float %offset, 4.0
  %pi = fptoui float %p4 to i32
  %fi = fptoui float %f4 to i32
  %oi = fptoui float %o4 to i32
  %base0 = mul i32 %tid, 12
  %base = add i32 %base0, 4
  %b1 = add i32 %base, 4
  %b2 = add i32 %base, 8
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %pi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %fi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b2, i32 poison, i32 %oi)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;
;--- heap.yaml
resource-heap:
  - index: 1
    size: 68
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: 2d-array
    array-layers: 2
    extent: [2, 1]
    format: r32_float
    data: [1048576000, 1061158912, 1061158912, 1048576000]
samplers:
  - index: 0
    compare-enable: true
    compare-func: less
    address-u: clamp-to-edge
    address-v: clamp-to-edge
  - index: 1
    min-filter: linear
    mag-filter: linear
    compare-enable: true
    compare-func: less
    address-u: clamp-to-edge
    address-v: clamp-to-edge
