; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,feme-cpu-lower-resources,verify' -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; CHECK-LABEL: define float @dynamic_offset(<2 x i32> %offset)
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @unsupported_3d()
; CHECK: call float @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define <4 x float> @unsupported_vector()
; CHECK: call <4 x float> @llvm.dx.resource.samplecmplevelzero
; CHECK-NOT: call float @feme.cpu.image.samplecmp
; CHECK-LABEL: define float @comparison(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call float @feme.cpu.image.samplecmp.2d.f32({{.*}}i32 %image, i32 %sampler, float {{.*}}, float {{.*}}, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float %ref, float 0.000000e+00, i32 -1, i32 2, float -inf, i1 true)
define float @comparison(i32 %image, i32 %sampler, <2 x float> %coord, float %ref) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 %image)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 %sampler)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %ref, <2 x i32> <i32 -1, i32 2>)
  ret float %v
}
;
define float @dynamic_offset(<2 x i32> %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> %offset)
  ret float %v
}
define float @unsupported_3d() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_4t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_4t.tdx.Sampler_1t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 1, 4) %t, target("dx.Sampler", 1) %s, <3 x float> zeroinitializer, float 0.5, <3 x i32> zeroinitializer)
  ret float %v
}
define <4 x float> @unsupported_vector() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call <4 x float> @llvm.dx.resource.samplecmplevelzero.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer)
  ret <4 x float> %v
}
; CHECK: !feme.cpu.resources = !{![[RES:[0-9]+]]}
; CHECK: ![[RES]] = !{!"comparison", i32 0, i1 true
declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_4t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_4t.tdx.Sampler_1t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 1, 4), target("dx.Sampler", 1), <3 x float>, float, <3 x i32>)
declare <4 x float> @llvm.dx.resource.samplecmplevelzero.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
