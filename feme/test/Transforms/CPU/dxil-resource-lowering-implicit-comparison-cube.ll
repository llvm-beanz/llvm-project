; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @nonzero_offset()
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @missing_layer(<3 x float> %coord)
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @llvm.dx.resource.samplecmp
; CHECK-NOT: @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @comparison_cube(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK-NOT: call target("dx.
; CHECK: [[X:%.*]] = extractelement <3 x float> %dir, i64 0
; CHECK: [[Y:%.*]] = extractelement <3 x float> %dir, i64 1
; CHECK: [[Z:%.*]] = extractelement <3 x float> %dir, i64 2
; CHECK: [[XX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[X]])
; CHECK: [[XY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[X]])
; CHECK: [[YX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[Y]])
; CHECK: [[YY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[Y]])
; CHECK: [[ZX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[Z]])
; CHECK: [[ZY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[Z]])
; CHECK: call float @feme.cpu.image.samplecmp.cube.f32(
; CHECK-SAME: i32 %image, i32 %sampler, float [[X]], float [[Y]], float [[Z]], float [[XX]], float [[XY]], float [[YX]], float [[YY]], float [[ZX]], float [[ZY]], float 0.000000e+00, i1 false, float %ref, float 0.000000e+00, float -inf, i1 true)
define float @comparison_cube(i32 %image, i32 %sampler, <3 x float> %dir, float %ref) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %dir, float %ref, i32 poison)
  ret float %r
}
; CHECK-LABEL: define float @comparison_array(
; CHECK: [[AX:%.*]] = extractelement <4 x float> %coord, i64 0
; CHECK: [[AY:%.*]] = extractelement <4 x float> %coord, i64 1
; CHECK: [[AZ:%.*]] = extractelement <4 x float> %coord, i64 2
; CHECK: [[AXX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[AX]])
; CHECK: [[AXY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[AX]])
; CHECK: [[AYX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[AY]])
; CHECK: [[AYY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[AY]])
; CHECK: [[AZX:%.*]] = call float @feme.stage.derivative.x.coarse.f32(float [[AZ]])
; CHECK: [[AZY:%.*]] = call float @feme.stage.derivative.y.coarse.f32(float [[AZ]])
; CHECK: [[LAYER:%.*]] = extractelement <4 x float> %coord, i64 3
; CHECK-NOT: @feme.stage.derivative
; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32(
; CHECK-SAME: float [[AX]], float [[AY]], float [[AZ]], float [[AXX]], float [[AXY]], float [[AYX]], float [[AYY]], float [[AZX]], float [[AZY]], float [[LAYER]], float 0.000000e+00, i1 false, float %ref, float 0.000000e+00, float %clamp, i1 true)
define float @comparison_array(<4 x float> %coord, float %ref, float %clamp) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <4 x float> %coord, float %ref, i32 0, float %clamp)
  ret float %r
}
define float @nonzero_offset() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.5, i32 1)
  ret float %r
}
define float @missing_layer(<3 x float> %coord) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float 0.5, i32 poison)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]], !{{[0-9]+}}}
; CHECK: ![[RES]] = !{!"comparison_cube", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32, float)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <3 x float>, float, i32)
attributes #0 = { "feme.shader.stage"="fragment" }
