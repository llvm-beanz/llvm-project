; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Five lanes cover plain/clamped 1D/array forms, signed scalar gradients/offsets,
; divergent layers/clamps, scalar/vector results, partial waves and repeats.
; Compiled-stage tests cover multi-mip LOD; runner YAML prepares one mip only.
; CHECK: heap[2]: 42 12 2 11 1 12 2 6 16 12 2 11 1 12 2 6 16 12 2 11 1 42
;--- shader.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %a = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 1)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %bit = and i32 %tid, 1
  %odd = icmp ne i32 %bit, 0
  %layer = uitofp i32 %bit to float
  %coord = insertelement <2 x float> <float 0.375, float poison>, float %layer, i32 1
  %dx = select i1 %odd, float 1.0, float 0.25
  %dy = fneg float %dx
  %clamp = select i1 %odd, float 0.0, float 2.0
  %p = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.375, float %dx, float %dy, i32 1)
  %pc = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.375, float %dx, float %dy, i32 -1, float %clamp)
  %ap = call <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %a, target("dx.Sampler", 0) %s, <2 x float> %coord, float %dx, float %dy, i32 1)
  %ac = call float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %a, target("dx.Sampler", 0) %s, <2 x float> %coord, float %dx, float %dy, i32 -1, float %clamp)
  %alpha = extractelement <4 x float> %p, i32 3
  %green = extractelement <2 x float> %pc, i32 1
  %blue = extractelement <3 x float> %ap, i32 2
  %pi = fptoui float %alpha to i32
  %pci = fptoui float %green to i32
  %api = fptoui float %blue to i32
  %aci = fptoui float %ac to i32
  %base0 = mul i32 %tid, 16
  %base = add i32 %base0, 4
  %b1 = add i32 %base, 4
  %b2 = add i32 %base, 8
  %b3 = add i32 %base, 12
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %pi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %pci)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b2, i32 poison, i32 %api)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b3, i32 poison, i32 %aci)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, float, i32)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, float, i32, float)
declare <3 x float> @llvm.dx.resource.samplegrad.v3f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, float, i32)
declare float @llvm.dx.resource.samplegrad.clamp.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, float, i32, float)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;--- heap.yaml
resource-heap:
  - index: 2
    size: 88
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - index: 0
    dimension: 1d
    extent: [4]
    format: r32g32b32a32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432, 1084227584, 1086324736, 1088421888, 1090519040, 1091567616, 1092616192, 1093664768, 1094713344, 1095761920, 1096810496, 1097859072, 1098907648]
  - index: 1
    dimension: 1d-array
    array-layers: 2
    extent: [4]
    format: r32g32b32a32_float
    data: [1065353216, 1073741824, 1077936128, 1082130432, 1084227584, 1086324736, 1088421888, 1090519040, 1091567616, 1092616192, 1093664768, 1094713344, 1095761920, 1096810496, 1097859072, 1098907648, 1098907648, 1097859072, 1096810496, 1095761920, 1094713344, 1093664768, 1092616192, 1091567616, 1090519040, 1088421888, 1086324736, 1084227584, 1082130432, 1077936128, 1073741824, 1065353216]
samplers:
  - index: 0
    address-u: clamp-to-edge
