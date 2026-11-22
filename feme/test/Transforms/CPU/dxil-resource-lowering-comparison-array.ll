; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; CHECK-LABEL: define float @missing_layer(<2 x float> %coord)
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @comparison_array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: %[[LAYER:.*]] = extractelement <3 x float> %coord, i64 2
; CHECK: call float @feme.cpu.image.samplecmp.2darray.f32({{.*}}i32 %image, i32 %sampler, float {{.*}}, float {{.*}}, float %[[LAYER]], float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, i32 1, i32 -2, float -inf, i1 true)
define float @comparison_array(i32 %image, i32 %sampler, <3 x float> %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, <2 x i32> <i32 1, i32 -2>)
  ret float %v
}
define float @missing_layer(<2 x float> %coord) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
  ret float %v
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]]}
; CHECK: ![[RES]] = !{!"comparison_array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
