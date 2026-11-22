; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define float @comparison_2d(
define float @comparison_2d(<2 x float> %coord, float %reference) {
  ; CHECK: [[T:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding{{.*}}(i32 7, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: [[S:%.*]] = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 7, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: [[R:%.*]] = call float @llvm.dx.resource.samplecmplevelzero{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[T]], target("dx.Sampler", 1) [[S]], <2 x float> {{.*}}, float %reference, <2 x i32> <i32 1, i32 -2>)
  ; CHECK: ret float [[R]]
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32 7, i32 3, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 7, i32 3, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %reference, <2 x i32> <i32 1, i32 -2>)
  ret float %v
}

; CHECK-LABEL: define float @comparison_1d(
define float @comparison_1d(float %coord, float %reference) {
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: float %coord, float %reference, i32 1)
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float %coord, float %reference, i32 1)
  ret float %v
}

; CHECK-LABEL: define float @comparison_cube_array(
define float @comparison_cube_array(<4 x float> %coord, float %reference) {
  ; CHECK: insertelement <4 x float> {{.*}}, float {{.*}}, i32 3
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: <4 x float> {{.*}}, float %reference, i32 poison)
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_9t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <4 x float> %coord, float %reference, i32 poison)
  ret float %v
}

; CHECK-LABEL: define float @comparison_2d_array(
define float @comparison_2d_array(<3 x float> %coord, float %reference) {
  ; CHECK: insertelement <3 x float> {{.*}}, float {{.*}}, i32 2
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: <3 x float> {{.*}}, float %reference, <2 x i32> <i32 1, i32 -2>)
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_7t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %reference, <2 x i32> <i32 1, i32 -2>)
  ret float %v
}

; CHECK-LABEL: define float @comparison_1d_array(
define float @comparison_1d_array(<2 x float> %coord, float %reference) {
  ; CHECK: insertelement <2 x float> {{.*}}, float {{.*}}, i32 1
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: <2 x float> {{.*}}, float %reference, i32 -1)
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_6t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %reference, i32 -1)
  ret float %v
}

; CHECK-LABEL: define float @comparison_cube(
define float @comparison_cube(<3 x float> %coord, float %reference) {
  ; CHECK: insertelement <3 x float> {{.*}}, float {{.*}}, i32 2
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: <3 x float> {{.*}}, float %reference, i32 poison)
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_5t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5) %t, target("dx.Sampler", 1) %s, <3 x float> %coord, float %reference, i32 poison)
  ret float %v
}

declare target("dx.Texture", <4 x float>, 0, 0, 1, 5) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_5t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_5t.tdx.Sampler_1t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 5), target("dx.Sampler", 1), <3 x float>, float, i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 6) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_6t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_6t.tdx.Sampler_1t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 6), target("dx.Sampler", 1), <2 x float>, float, i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_7t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <3 x float>, float, <2 x i32>)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_1t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_9t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32, i32, i32, i32, ptr)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
