; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer()
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplebias
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <4 x float> @compute()
; CHECK-NOT: @feme.stage.derivative
; CHECK: call <4 x float> @llvm.dx.resource.samplebias
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <2 x float> @clamped(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <3 x float> %coord, i64 0
; CHECK: [[V:%.*]] = extractelement <3 x float> %coord, i64 1
; CHECK: [[UX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[U]])
; CHECK: [[UY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[U]])
; CHECK: [[VX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[V]])
; CHECK: [[VY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[V]])
; CHECK: [[LAYER:%.*]] = extractelement <3 x float> %coord, i64 2
; CHECK-NOT: @feme.stage.derivative
; CHECK: [[SAMPLE:%.*]] = call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[U]], float [[V]], float [[LAYER]], float [[UX]], float [[UY]], float [[VX]], float [[VY]], float 0.000000e+00, i1 false, float %bias, i32 -1, i32 2, float %clamp, i1 true)
; CHECK: shufflevector <4 x float> [[SAMPLE]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define <2 x float> @clamped(i32 %image, i32 %sampler, <3 x float> %coord, float %bias, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %bias, <2 x i32> <i32 -1, i32 2>, float %clamp)
  ret <2 x float> %r
}
define float @missing_layer() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  ret float %r
}
define <4 x float> @compute() #1 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]]}
; CHECK: ![[RES]] = !{!"clamped", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>, float)
declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
attributes #1 = { "feme.shader.stage"="compute" }
