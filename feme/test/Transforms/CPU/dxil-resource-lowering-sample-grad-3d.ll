; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @bad_dx(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @bad_dy(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @missing_w(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @short_offset(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @dynamic_offset(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <3 x float> %coord, i64 0
; CHECK: [[V:%.*]] = extractelement <3 x float> %coord, i64 1
; CHECK: [[UX:%.*]] = extractelement <3 x float> %dx, i64 0
; CHECK: [[UY:%.*]] = extractelement <3 x float> %dy, i64 0
; CHECK: [[VX:%.*]] = extractelement <3 x float> %dx, i64 1
; CHECK: [[VY:%.*]] = extractelement <3 x float> %dy, i64 1
; CHECK: [[W:%.*]] = extractelement <3 x float> %coord, i64 2
; CHECK: [[WX:%.*]] = extractelement <3 x float> %dx, i64 2
; CHECK: [[WY:%.*]] = extractelement <3 x float> %dy, i64 2
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.3d.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[U]], float [[V]], float [[W]], float [[UX]], float [[UY]], float [[VX]], float [[VY]], float [[WX]], float [[WY]], float 0.000000e+00, i1 false, float 0.000000e+00, i32 1, i32 -1, i32 2, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @clamped(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[C:%.*]] = call <4 x float> @feme.cpu.image.sample.3d.v4f32(
; CHECK-SAME: float 1.250000e-01, float -2.500000e-01, float 5.000000e-01, float -7.500000e-01, float 1.000000e+00, float -2.000000e+00, float 0.000000e+00, i1 false, float 0.000000e+00, i32 -1, i32 2, i32 -3, float %clamp, i1 true)
; CHECK: shufflevector <4 x float> [[C]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, i32 %sampler, <3 x float> %coord, <3 x float> %dx, <3 x float> %dy) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, <3 x float> %dx, <3 x float> %dy, <3 x i32> <i32 1, i32 -1, i32 2>)
  ret float %r
}
define <2 x float> @clamped(float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> <float 0.125, float 0.5, float 1.0>, <3 x float> <float -0.25, float -0.75, float -2.0>, <3 x i32> <i32 -1, i32 2, i32 -3>, float %clamp)
  ret <2 x float> %r
}
define float @bad_dx() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v2f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <2 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x i32> zeroinitializer)
  ret float %r
}
define float @bad_dy() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v4f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <4 x float> zeroinitializer, <3 x i32> zeroinitializer)
  ret float %r
}
define float @missing_w() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v2f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x i32> zeroinitializer)
  ret float %r
}
define float @short_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <2 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(<3 x i32> %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x i32> %offset)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[CLAMP:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 true
; CHECK: ![[CLAMP]] = !{!"clamped", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <3 x i32>)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <3 x i32>, float)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v2f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <2 x float>, <3 x float>, <3 x i32>)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v4f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <4 x float>, <3 x i32>)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v2f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <2 x float>, <3 x float>, <3 x float>, <3 x i32>)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <2 x i32>)
attributes #0 = { "feme.shader.stage"="compute" }
