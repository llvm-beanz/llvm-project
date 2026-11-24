; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK-LABEL: define float @missing_layer(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @padded_offset(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @dynamic_offset(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define <4 x float> @too_wide(
; CHECK: call <4 x float> @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @comparison_sampler(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @non_heap(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @non_fp32(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @cube(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.cube.v4f32(
; CHECK-SAME: float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %lod, i1 true, float 0.000000e+00, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <4 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[L:%.*]] = extractelement <4 x float> %coord, i64 3
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.cubearray.v4f32(
; CHECK-SAME: float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float [[L]], float %lod, i1 true, float 0.000000e+00, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK: [[B:%.*]] = call <4 x float> @feme.cpu.image.sample.cubearray.v4f32(
; CHECK-SAME: float %lod, i1 true, float 0.000000e+00, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[B]], <4 x float> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: call <4 x float> @feme.cpu.image.sample.cubearray.v4f32(
; CHECK-SAME: float %lod, i1 true, float 0.000000e+00, float -inf, i1 true)
; CHECK-NOT: call target("dx.
; CHECK: !feme.cpu.resources = !{![[CUBE:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[CUBE]] = !{!"cube", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 true
define float @cube(i32 %image, i32 %sampler, <3 x float> %dir, float %lod) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> %dir, float %lod, i32 0)
  ret float %r
}
define <4 x float> @array(<4 x float> %coord, float %lod) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 1)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %rg = call <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> %coord, float %lod, i32 undef)
  %rgb = call <3 x float> @llvm.dx.resource.samplelevel.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> %coord, float %lod, i32 poison)
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> %coord, float %lod, i32 0)
  ret <4 x float> %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 1)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, i32 0)
  ret float %r
}
define float @padded_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(i32 %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, i32 %offset)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, i32 0)
  ret <4 x float> %r
}
define float @comparison_sampler() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.0, i32 0)
  ret float %r
}
define float @non_heap(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t) {
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, i32 0)
  ret float %r
}
define float @non_fp32() {
  %t = call target("dx.Texture", <4 x half>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f16_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f16_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x half>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, i32 0)
  ret float %r
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
declare target("dx.Texture", float, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_5t(i32)
declare target("dx.Texture", <4 x half>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f16_0_0_0_5t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32)
declare <3 x float> @llvm.dx.resource.samplelevel.v3f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", float, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f16_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x half>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
attributes #0 = { "feme.shader.stage"="fragment" }
