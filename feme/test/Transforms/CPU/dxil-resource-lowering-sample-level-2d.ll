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
; CHECK-LABEL: define float @partial_offset(
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
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.sample.2d.v4f32(
; CHECK-SAME: float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %lod, i1 true, float 0.000000e+00, i32 1, i32 -1, float -inf, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <4 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[L:%.*]] = extractelement <3 x float> %coord, i64 2
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: float [[L]], float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %lod, i1 true, float 0.000000e+00, i32 -1, i32 1, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK: [[B:%.*]] = call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: float %lod, i1 true, float 0.000000e+00, i32 0, i32 0, float -inf, i1 true)
; CHECK: shufflevector <4 x float> [[B]], <4 x float> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: call <4 x float> @feme.cpu.image.sample.2darray.v4f32(
; CHECK-SAME: float %lod, i1 true, float 0.000000e+00, i32 1, i32 -1, float -inf, i1 true)
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, i32 %sampler, <2 x float> %coord, float %lod) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 %image)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 %sampler)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float %lod, <2 x i32> <i32 1, i32 -1>)
  ret float %r
}
define <4 x float> @array(<3 x float> %coord, float %lod) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %rg = call <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %lod, <2 x i32> <i32 -1, i32 1>)
  %rgb = call <3 x float> @llvm.dx.resource.samplelevel.v3f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %lod, <2 x i32> zeroinitializer)
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> %coord, float %lod, <2 x i32> <i32 1, i32 -1>)
  ret <4 x float> %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @padded_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 0.0, <3 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(<2 x i32> %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> %offset)
  ret float %r
}
define float @partial_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> <i32 poison, i32 0>)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
define float @comparison_sampler() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @non_heap(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t) {
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @implicit_unchanged() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, <2 x i32> <i32 1, i32 -1>)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[P:[0-9]+]], ![[A:[0-9]+]]}
; CHECK: ![[P]] = !{!"plain", i32 0, i1 true
; CHECK: ![[A]] = !{!"array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
declare target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_2t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>)
declare <2 x float> @llvm.dx.resource.samplelevel.v2f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare <3 x float> @llvm.dx.resource.samplelevel.v3f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", float, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplelevel.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
