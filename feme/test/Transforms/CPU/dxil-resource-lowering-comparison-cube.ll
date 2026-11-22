; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; CHECK-LABEL: define float @bad_offset(i32 %offset)
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @cube(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call float @feme.cpu.image.samplecmp.cube.f32({{.*}}i32 %image, i32 %sampler, float {{.*}}, float {{.*}}, float {{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, float -inf, i1 true)
; CHECK-LABEL: define float @cube_array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: %[[LAYER:.*]] = extractelement <4 x float> %coord, i64 3
; CHECK: call float @feme.cpu.image.samplecmp.cubearray.f32({{.*}}i32 %image, i32 %sampler, float {{.*}}, float {{.*}}, float {{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float %[[LAYER]], float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, float -inf, i1 true)
define float @cube(i32 %image, i32 %sampler, <3 x float> %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %ref, i32 poison)
  ret float %v
}
define float @cube_array(i32 %image, i32 %sampler, <4 x float> %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <4 x float> %coord, float %ref, i32 undef)
  ret float %v
}
define float @bad_offset(i32 %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.5, i32 %offset)
  ret float %v
}
; CHECK: !feme.cpu.resources = !{![[CUBE:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[CUBE]] = !{!"cube", i32 0, i1 true
; CHECK: ![[ARRAY]] = !{!"cube_array", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_5t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_9t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
