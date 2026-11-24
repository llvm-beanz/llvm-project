; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @vector_offset(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @dynamic_offset(
; CHECK: call float @llvm.dx.resource.samplelevel
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @poison_offset(
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
; CHECK-LABEL: define float @implicit_unchanged(
; CHECK: call float @llvm.dx.resource.sample.
; CHECK-NOT: @feme.cpu.image.sample
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.1d.v4f32(
; CHECK-SAME: i32 %image, i32 %sampler, float %coord, float 0.000000e+00, float 0.000000e+00, float %lod, i1 true, float 0.000000e+00, i32 -1, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[U:%.*]] = extractelement <2 x float> %coord, i64 0
; CHECK: [[L:%.*]] = extractelement <2 x float> %coord, i64 1
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.1darray.v4f32(
; CHECK-SAME: float [[U]], float [[L]], float 0.000000e+00, float 0.000000e+00, float %lod, i1 true, float 0.000000e+00, i32 1, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.

define float @plain(i32 %image, i32 %sampler, float %coord, float %lod) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %coord, float %lod, i32 -1)
  ret float %r
}
define <2 x float> @array(<2 x float> %coord, float %lod) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %lod, i32 1)
  ret <2 x float> %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, i32 0)
  ret float %r
}
define float @vector_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.v1i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, <1 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(i32 %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, i32 %offset)
  ret float %r
}
define float @poison_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, i32 poison)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, i32 0)
  ret <4 x float> %r
}
define float @comparison_sampler() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 1) %s, float 0.0, float 0.0, i32 0)
  ret float %r
}
define float @non_heap(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t) {
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, float 0.0, i32 0)
  ret float %r
}
define float @implicit_unchanged() #1 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.0, i32 0)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
declare <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), float, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.v1i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, <1 x i32>)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 1), float, float, i32)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, i32)
attributes #0 = { "feme.shader.stage"="compute" }
attributes #1 = { "feme.shader.stage"="compute" }
