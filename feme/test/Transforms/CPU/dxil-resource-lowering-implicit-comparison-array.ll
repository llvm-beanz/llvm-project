; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer(<2 x float> %coord)
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @compute(
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp.clamp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @comparison_array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK-NOT: call target("dx.
; CHECK: [[U:%.*]] = extractelement <3 x float> %coord, i64 0
; CHECK: [[V:%.*]] = extractelement <3 x float> %coord, i64 1
; CHECK: [[UX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[U]])
; CHECK: [[UY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[U]])
; CHECK: [[VX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[V]])
; CHECK: [[VY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[V]])
; CHECK: [[LAYER:%.*]] = extractelement <3 x float> %coord, i64 2
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[U]], float [[V]], float [[LAYER]], float [[UX]], float [[UY]], float [[VX]], float [[VY]], float 0.000000e+00, i1 false, float %ref, float 0.000000e+00, i32 -1, i32 2, float %clamp, i1 true)
; CHECK-NOT: @llvm.dx.resource.samplecmp
; CHECK: ret float
define float @comparison_array(i32 %image, i32 %sampler, <3 x float> %coord, float %ref, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, <2 x i32> <i32 -1, i32 2>, float %clamp)
  ret float %r
}
define float @missing_layer(<2 x float> %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
  ret float %r
}
define float @compute(<3 x float> %coord) #1 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float 0.5, <2 x i32> zeroinitializer, float 2.0)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]]}
; CHECK: ![[RES]] = !{!"comparison_array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>, float)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
attributes #1 = { "feme.shader.stage"="compute" }
