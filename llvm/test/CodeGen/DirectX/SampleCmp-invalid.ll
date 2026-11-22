; RUN: not opt -S -dxil-op-lower %s 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK: SampleCmp requires a scalar float result
; CHECK: SampleCmp requires a comparison sampler
target triple = "dxil-pc-shadermodel6.0-pixel"
define <4 x float> @bad_result(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s) {
  %v = call <4 x float> @llvm.dx.resource.samplecmp.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer)
  ret <4 x float> %v
}
define float @bad_sampler(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 0) %s) {
  %v = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer, float 1.0)
  ret float %v
}
declare <4 x float> @llvm.dx.resource.samplecmp.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
