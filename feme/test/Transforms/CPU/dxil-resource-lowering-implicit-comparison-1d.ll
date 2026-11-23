; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer(float %coord)
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @dynamic_offset(i32 %offset)
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @comparison_1d(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK-NOT: call target("dx.
; CHECK: [[UX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float %u)
; CHECK: [[UY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float %u)
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @feme.cpu.image.samplecmp.1d.f32(
; CHECK-SAME: i32 %image, i32 %sampler, float %u, float [[UX]], float [[UY]], float 0.000000e+00, i1 false, float %ref, float 0.000000e+00, i32 -1, float -inf, i1 true)
define float @comparison_1d(i32 %image, i32 %sampler, float %u, float %ref) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %u, float %ref, i32 -1)
  ret float %r
}
; CHECK-LABEL: define float @comparison_array(
; CHECK: [[U:%.*]] = extractelement <2 x float> %coord, i64 0
; CHECK: [[DX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[U]])
; CHECK: [[DY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[U]])
; CHECK: [[LAYER:%.*]] = extractelement <2 x float> %coord, i64 1
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @feme.cpu.image.samplecmp.1darray.f32(
; CHECK-SAME: float [[U]], float [[LAYER]], float [[DX]], float [[DY]], float 0.000000e+00, i1 false, float %ref, float 0.000000e+00, i32 2, float %clamp, i1 true)
define float @comparison_array(<2 x float> %coord, float %ref, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, i32 2, float %clamp)
  ret float %r
}
define float @missing_layer(float %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, float %coord, float 0.5, i32 0)
  ret float %r
}
define float @dynamic_offset(i32 %offset) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float 0.0, float 0.5, i32 %offset)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]], !{{[0-9]+}}}
; CHECK: ![[RES]] = !{!"comparison_1d", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32, float)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), float, float, i32)
attributes #0 = { "feme.shader.stage"="fragment" }
