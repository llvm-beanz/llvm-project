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
; Each lane stores plain/array point comparisons, plain/array linear
; comparisons, a negative array offset and a positive plain offset.
; CHECK: heap[2]: 42 0 0 2 2 0 4 4 4 2 2 0 4 0 4 2 2 4 4 4 0 2 2 4 4 0 0 2 2 0 4 42
;
;--- shader.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
  %a = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 1)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %linear = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 1)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %column = and i32 %tid, 1
  %shift = lshr i32 %tid, 1
  %layer = and i32 %shift, 1
  %lf = uitofp i32 %layer to float
  %cf = uitofp i32 %column to float
  %scaled = fmul float %cf, 0.5
  %u = fadd float %scaled, 0.25
  %c0 = insertelement <2 x float> poison, float %u, i32 0
  %coord = insertelement <2 x float> %c0, float %lf, i32 1
  %center = insertelement <2 x float> <float 0.5, float poison>, float %lf, i32 1
  %right = insertelement <2 x float> <float 0.75, float poison>, float %lf, i32 1
  %p = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %u, float 0.5, i32 0)
  %pa = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, i32 0)
  %l = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %linear, float 0.5, float 0.5, i32 0)
  %la = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %linear, <2 x float> %center, float 0.5, i32 0)
  %n = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %a, target("dx.Sampler", 1) %s, <2 x float> %right, float 0.5, i32 -1)
  %o = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float 0.25, float 0.5, i32 1)
  %p4 = fmul float %p, 4.0
  %pa4 = fmul float %pa, 4.0
  %l4 = fmul float %l, 4.0
  %la4 = fmul float %la, 4.0
  %n4 = fmul float %n, 4.0
  %o4 = fmul float %o, 4.0
  %pi = fptoui float %p4 to i32
  %pai = fptoui float %pa4 to i32
  %li = fptoui float %l4 to i32
  %lai = fptoui float %la4 to i32
  %ni = fptoui float %n4 to i32
  %oi = fptoui float %o4 to i32
  %b0 = mul i32 %tid, 24
  %base = add i32 %b0, 4
  %b1 = add i32 %base, 4
  %b2 = add i32 %base, 8
  %b3 = add i32 %base, 12
  %b4 = add i32 %base, 16
  %b5 = add i32 %base, 20
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %pi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %pai)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b2, i32 poison, i32 %li)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b3, i32 poison, i32 %lai)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b4, i32 poison, i32 %ni)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b5, i32 poison, i32 %oi)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;
;--- heap.yaml
resource-heap:
  - index: 2
    size: 128
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
           42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: 1d
    extent: [2]
    format: r32_float
    data: [1048576000, 1061158912]
  - index: 1
    dimension: 1d-array
    array-layers: 2
    extent: [2]
    format: r32_float
    data: [1048576000, 1061158912, 1061158912, 1048576000]
samplers:
  - index: 0
    compare-enable: true
    compare-func: less
    address-u: clamp-to-edge
  - index: 1
    min-filter: linear
    mag-filter: linear
    compare-enable: true
    compare-func: less
    address-u: clamp-to-edge
