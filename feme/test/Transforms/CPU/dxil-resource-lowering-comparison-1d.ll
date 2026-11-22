; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; CHECK-LABEL: define float @wrong_coord(<2 x float> %coord)
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @wrong_offset(<2 x i32> %offset)
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define <4 x float> @ordinary_1d(float %coord)
; CHECK: call <4 x float> @llvm.dx.resource.samplelevel
; CHECK-NOT: call {{.*}} @feme.cpu.image.sample
; CHECK-LABEL: define float @comparison_1d(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call float @feme.cpu.image.samplecmp.1d.f32({{.*}}i32 %image, i32 %sampler, float %coord, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, i32 -1, float -inf, i1 true)
; CHECK-LABEL: define float @comparison_1d_array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: %[[LAYER:.*]] = extractelement <2 x float> %coord, i64 1
; CHECK: call float @feme.cpu.image.samplecmp.1darray.f32({{.*}}i32 %image, i32 %sampler, float {{.*}}, float %[[LAYER]], float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, i32 1, float -inf, i1 true)
define float @comparison_1d(i32 %image, i32 %sampler, float %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %coord, float %ref, i32 -1)
  ret float %v
}
define float @comparison_1d_array(i32 %image, i32 %sampler, <2 x float> %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, i32 1)
  ret float %v
}
define float @wrong_coord(<2 x float> %coord) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, i32 0)
  ret float %v
}
define float @wrong_offset(<2 x i32> %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> %offset)
  ret float %v
}
define <4 x float> @ordinary_1d(float %coord) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %v = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_0t.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 0) %s, float %coord, float 0.0, i32 0)
  ret <4 x float> %v
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"comparison_1d", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"comparison_1d_array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_6t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), <2 x float>, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_0t.f32.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 0), float, float, i32)
