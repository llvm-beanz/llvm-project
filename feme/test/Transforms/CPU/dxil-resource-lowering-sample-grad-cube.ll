; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @nonzero_offset(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @missing_layer(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @layer_gradient(
; CHECK: call float @llvm.dx.resource.samplegrad
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @cube(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <3 x float> %dir, i64 0
; CHECK: [[Y:%.*]] = extractelement <3 x float> %dir, i64 1
; CHECK: [[Z:%.*]] = extractelement <3 x float> %dir, i64 2
; CHECK: [[XX:%.*]] = extractelement <3 x float> %dx, i64 0
; CHECK: [[XY:%.*]] = extractelement <3 x float> %dy, i64 0
; CHECK: [[YX:%.*]] = extractelement <3 x float> %dx, i64 1
; CHECK: [[YY:%.*]] = extractelement <3 x float> %dy, i64 1
; CHECK: [[ZX:%.*]] = extractelement <3 x float> %dx, i64 2
; CHECK: [[ZY:%.*]] = extractelement <3 x float> %dy, i64 2
; CHECK: [[C:%.*]] = call <4 x float> @feme.cpu.image.sample.cube.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[X]], float [[Y]], float [[Z]], float [[XX]], float [[XY]], float [[YX]], float [[YY]], float [[ZX]], float [[ZY]], float 0.000000e+00, i1 false, float 0.000000e+00, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[C]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[AX:%.*]] = extractelement <4 x float> %coord, i64 0
; CHECK: [[AY:%.*]] = extractelement <4 x float> %coord, i64 1
; CHECK: [[AZ:%.*]] = extractelement <4 x float> %coord, i64 2
; CHECK: [[L:%.*]] = extractelement <4 x float> %coord, i64 3
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.cubearray.v4f32(
; CHECK-SAME: float [[AX]], float [[AY]], float [[AZ]], float 1.250000e-01, float -2.500000e-01, float 5.000000e-01, float -7.500000e-01, float 1.000000e+00, float -2.000000e+00, float [[L]], float 0.000000e+00, i1 false, float 0.000000e+00, float %clamp, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @cube(i32 %image, i32 %sampler, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, <3 x float> %dx, <3 x float> %dy, i32 poison)
  ret float %r
}
define <2 x float> @array(<4 x float> %coord, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> %coord, <3 x float> <float 0.125, float 0.5, float 1.0>, <3 x float> <float -0.25, float -0.75, float -2.0>, i32 0, float %clamp)
  ret <2 x float> %r
}
define float @nonzero_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x float> zeroinitializer, i32 1)
  ret float %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> zeroinitializer, <3 x float> zeroinitializer, i32 poison)
  ret float %r
}
define float @layer_gradient() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v4f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> zeroinitializer, <4 x float> zeroinitializer, <3 x float> zeroinitializer, i32 poison)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[CUBE:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[CUBE]] = !{!"cube", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
declare <2 x float> @llvm.dx.resource.samplegrad.clamp.v2f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <3 x float>, <3 x float>, i32, float)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v3f32.v3f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, i32)
declare float @llvm.dx.resource.samplegrad.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.v4f32.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, <4 x float>, <3 x float>, i32)
attributes #0 = { "feme.shader.stage"="compute" }
