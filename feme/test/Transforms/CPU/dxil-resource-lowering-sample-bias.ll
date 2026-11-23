; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <4 x float> @compute()
; CHECK-NOT: @feme.stage.derivative
; CHECK: call <4 x float> @llvm.dx.resource.samplebias
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <4 x float> @dynamic_offset(<2 x i32> %offset)
; CHECK: call <4 x float> @llvm.dx.resource.samplebias
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <4 x float> @comparison_sampler()
; CHECK: call <4 x float> @llvm.dx.resource.samplebias
; CHECK-NOT: @feme.cpu.image.sample

; CHECK-LABEL: define float @biased(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <2 x float> %coord, i64 0
; CHECK: [[V:%.*]] = extractelement <2 x float> %coord, i64 1
; CHECK: [[UX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[U]])
; CHECK: [[UY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[U]])
; CHECK: [[VX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[V]])
; CHECK: [[VY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[V]])
; CHECK: [[SAMPLE:%.*]] = call <4 x float> @feme.cpu.image.sample.2d.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[U]], float [[V]], float [[UX]], float [[UY]], float [[VX]], float [[VY]], float 0.000000e+00, i1 false, float %bias, i32 -1, i32 2, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[SAMPLE]], i64 0
; CHECK-NOT: call target("dx.
define float @biased(i32 %image, i32 %sampler, <2 x float> %coord, float %bias) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 -1, i32 2>)
  ret float %r
}
; CHECK-LABEL: define <2 x float> @clamped(
; CHECK: [[CLAMPED:%.*]] = call <4 x float> @feme.cpu.image.sample.2d.v4f32(
; CHECK-SAME: i1 false, float %bias, i32 1, i32 -2, float %clamp, i1 true)
; CHECK: shufflevector <4 x float> [[CLAMPED]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define <2 x float> @clamped(<2 x float> %coord, float %bias, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %bias, <2 x i32> <i32 1, i32 -2>, float %clamp)
  ret <2 x float> %r
}
define <4 x float> @compute() #1 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
define <4 x float> @dynamic_offset(<2 x i32> %offset) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> %offset)
  ret <4 x float> %r
}
define <4 x float> @array() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
define <4 x float> @comparison_sampler() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
; CHECK-LABEL: define <4 x float> @array(
; CHECK: call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: i1 false, float 1.000000e+00, i32 0, i32 0, float -inf, i1 true)
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]], !{{[0-9]+}}, !{{[0-9]+}}}
; CHECK: ![[RES]] = !{!"biased", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
attributes #1 = { "feme.shader.stage"="compute" }
