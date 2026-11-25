; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; All faces, both cube-array elements, divergent gradients/clamps, scalar/vector
; results, partial waves and guarded repeats. This canonical fixture uses
; one mip; native heap-sample-grad-cube.test also executes three real mips
; with direction projection, scaled directions and shader/sampler clamps.
; CHECK: heap[2]: 42 1 0 1 1 2 0 2 2 3 0 3 3 4 0 4 4 5 0 5 5 6 0 6 6
; CHECK-SAME: 1 0 6 6 2 0 5 5 3 0 4 4 4 0 3 3 5 0 2 2 6 0 1 1 1 0 1 1 42
;--- shader.ll
@directions = private constant [6 x <3 x float>] [<3 x float> <float 1.0, float 0.0, float 0.0>, <3 x float> <float -1.0, float 0.0, float 0.0>, <3 x float> <float 0.0, float 1.0, float 0.0>, <3 x float> <float 0.0, float -1.0, float 0.0>, <3 x float> <float 0.0, float 0.0, float 1.0>, <3 x float> <float 0.0, float 0.0, float -1.0>]
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %a = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 1)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %face = urem i32 %tid, 6
  %ptr = getelementptr [6 x <3 x float>], ptr @directions, i32 0, i32 %face
  %dir = load <3 x float>, ptr %ptr
  %cube = udiv i32 %tid, 6
  %layeri = and i32 %cube, 1
  %layer = uitofp i32 %layeri to float
  %wide = shufflevector <3 x float> %dir, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
  %coord = insertelement <4 x float> %wide, float %layer, i32 3
  %bit = and i32 %tid, 1
  %odd = icmp ne i32 %bit, 0
  %g = select i1 %odd, float 1.0, float 0.25
  %clamp = select i1 %odd, float 0.0, float 2.0
  %dx = insertelement <3 x float> zeroinitializer, float %g, i32 0
  %ng = fneg float %g
  %dy = insertelement <3 x float> zeroinitializer, float %ng, i32 2
  %cp = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy, i32 poison)
  %cc = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy, i32 0, float %clamp)
  %ap = call <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> %dy, i32 undef)
  %ac = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %a, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> %dx, <3 x float> %dy, i32 poison, float %clamp)
  %c = extractelement <4 x float> %cp, i32 0
  %cg = extractelement <2 x float> %cc, i32 1
  %ar = extractelement <3 x float> %ap, i32 0
  %ci = fptoui float %c to i32
  %cgi = fptoui float %cg to i32
  %ai = fptoui float %ar to i32
  %aci = fptoui float %ac to i32
  %base0 = mul i32 %tid, 16
  %base = add i32 %base0, 4
  %b1 = add i32 %base, 4
  %b2 = add i32 %base, 8
  %b3 = add i32 %base, 12
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %ci)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %cgi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b2, i32 poison, i32 %ai)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b3, i32 poison, i32 %aci)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32, float)
declare <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32)
declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="13,1,1" }
;--- heap.yaml
resource-heap:
  - index: 2
    size: 216
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
           42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42,
           42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: cube
    extent: [1, 1]
    format: r32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432, 1084227584, 1086324736]
  - index: 1
    dimension: cube-array
    array-layers: 12
    extent: [1, 1]
    format: r32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432, 1084227584, 1086324736,
           1086324736, 1084227584, 1082130432, 1077936128, 1073741824, 1065353216]
samplers:
  - index: 0
