; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; 1D gradients/offsets are scalar even when coordinates include an array layer.
; CHECK-LABEL: define float @bad_dx(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @bad_dy(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @missing_layer(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @wrong_offset(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.1d.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float %coord, float %dx, float %dy, float 0.000000e+00, i1 false, float 0.000000e+00, i32 -1, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <2 x float> %coord, i64 0
; CHECK: [[L:%.*]] = extractelement <2 x float> %coord, i64 1
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.1darray.v4f32(
; CHECK-SAME: float [[U]], float [[L]], float 1.250000e-01, float -5.000000e-01, float 0.000000e+00, i1 false, float 0.000000e+00, i32 1, float %clamp, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, i32 %sampler, float %coord, float %dx, float %dy) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %coord, float %dx, float %dy, i32 -1)
  ret float %r
}
define <2 x float> @array(<2 x float> %coord, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float 0.125, float -0.5, i32 1, float %clamp)
  ret <2 x float> %r
}
define float @bad_dx() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.v1f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, <1 x float> zeroinitializer, float 0.0, i32 0)
  ret float %r
}
define float @bad_dy() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x float> zeroinitializer, i32 0)
  ret float %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, float 0.0, i32 0)
  ret float %r
}
define float @wrong_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, float 0.0, <2 x i32> zeroinitializer)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, float, i32)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, float, i32, float)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.v1f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, <1 x float>, float, i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, <2 x float>, i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), float, float, float, i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.f32.f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, float, <2 x i32>)
attributes #0 = { "feme.shader.stage"="compute" }
