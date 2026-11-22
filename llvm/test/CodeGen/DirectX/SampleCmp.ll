; RUN: opt -S -dxil-op-lower %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target triple = "dxil-pc-shadermodel6.0-pixel"

; CHECK-LABEL: define float @comparison(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64,
; CHECK-SAME: i32 1, i32 -2, i32 undef, float %reference, float undef)
; CHECK: extractvalue %dx.types.ResRet.f32 {{.*}}, 0
define float @comparison(<2 x float> %coord, float %reference) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %reference, <2 x i32> <i32 1, i32 -2>)
  ret float %r
}
; CHECK-LABEL: define float @clamped(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64,
; CHECK-SAME: float %reference, float %clamp)
define float @clamped(<2 x float> %coord, float %reference, float %clamp) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %reference, <2 x i32> zeroinitializer, float %clamp)
  ret float %r
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32, i32, i32, i32, ptr)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplecmp.clamp.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>, float)
