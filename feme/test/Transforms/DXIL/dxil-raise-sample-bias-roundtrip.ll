; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | FileCheck %s --check-prefix=WIRE
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
target triple = "dxil-pc-shadermodel6.6-pixel"

; CHECK-LABEL: define float @one_d(
; CHECK: call float @llvm.dx.resource.samplebias.f32
; CHECK-SAME: float %u, float %bias, i32 -1)
; WIRE-LABEL: define float @one_d(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: float %u, float undef, float undef, float undef, i32 -1, i32 undef, i32 undef, float %bias, float undef)
define float @one_d(float %u, float %bias) {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefrombinding.tdx.Texture_f32_0_0_0_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call float @llvm.dx.resource.samplebias.f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float %u, float %bias, i32 -1)
  ret float %r
}
; CHECK-LABEL: define <2 x float> @one_d_array(
; CHECK: call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32
; CHECK-SAME: <2 x float> <float 2.500000e-01, float 1.000000e+00>, float %bias, i32 2, float 1.250000e+00)
; WIRE-LABEL: define <2 x float> @one_d_array(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: float %bias, float 1.250000e+00)
define <2 x float> @one_d_array(float %bias) {
  %t = call target("dx.Texture", <2 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefrombinding.tdx.Texture_v2f32_0_0_0_6t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v2f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <2 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> <float 0.25, float 1.0>, float %bias, i32 2, float 1.25)
  ret <2 x float> %r
}
; CHECK-LABEL: define <3 x float> @two_d(
; CHECK: call <3 x float> @llvm.dx.resource.samplebias.v3f32
; CHECK-SAME: <2 x float> <float 2.500000e-01, float 7.500000e-01>, float %bias, <2 x i32> <i32 1, i32 -2>)
; WIRE-LABEL: define <3 x float> @two_d(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: float 2.500000e-01, float 7.500000e-01, float undef, float undef, i32 1, i32 -2, i32 undef, float %bias, float undef)
define <3 x float> @two_d(float %bias) {
  %t = call target("dx.Texture", <3 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v3f32_0_0_0_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v3f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <3 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> <float 0.25, float 0.75>, float %bias, <2 x i32> <i32 1, i32 -2>)
  ret <3 x float> %r
}
; CHECK-LABEL: define <4 x float> @two_d_array(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32
; CHECK-SAME: <3 x float> <float 2.500000e-01, float 7.500000e-01, float 2.000000e+00>, float %bias, <2 x i32> <i32 -1, i32 2>, float 0.000000e+00)
; WIRE-LABEL: define <4 x float> @two_d_array(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: float %bias, float 0.000000e+00)
define <4 x float> @two_d_array(float %bias) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_7t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> <float 0.25, float 0.75, float 2.0>, float %bias, <2 x i32> <i32 -1, i32 2>, float 0.0)
  ret <4 x float> %r
}
; CHECK-LABEL: define <4 x float> @three_d(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.v4f32
; CHECK-SAME: <3 x float> <float 2.500000e-01, float 5.000000e-01, float 7.500000e-01>, float %bias, <3 x i32> <i32 -1, i32 2, i32 3>)
; WIRE-LABEL: define <4 x float> @three_d(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: i32 -1, i32 2, i32 3, float %bias, float undef)
define <4 x float> @three_d(float %bias) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_4t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> <float 0.25, float 0.5, float 0.75>, float %bias, <3 x i32> <i32 -1, i32 2, i32 3>)
  ret <4 x float> %r
}
; CHECK-LABEL: define <4 x float> @cube(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.v4f32
; CHECK-SAME: <3 x float> <float 2.500000e-01, float 7.500000e-01, float 1.000000e+00>, float %bias, i32 poison)
; WIRE-LABEL: define <4 x float> @cube(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: i32 poison, i32 undef, i32 undef, float %bias, float undef)
define <4 x float> @cube(float %bias) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_5t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> <float 0.25, float 0.75, float 1.0>, float %bias, i32 poison)
  ret <4 x float> %r
}
; CHECK-LABEL: define <4 x float> @cube_array(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32
; CHECK-SAME: <4 x float> <float 2.500000e-01, float 5.000000e-01, float 7.500000e-01, float 1.000000e+00>, float %bias, i32 poison, float %clamp)
; WIRE-LABEL: define <4 x float> @cube_array(
; WIRE: @dx.op.sampleBias.f32(i32 61,
; WIRE-SAME: i32 poison, i32 undef, i32 undef, float %bias, float %clamp)
define <4 x float> @cube_array(float %bias, float %clamp) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_9t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> <float 0.25, float 0.5, float 0.75, float 1.0>, float %bias, i32 poison, float %clamp)
  ret <4 x float> %r
}
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding.tdx.Sampler_0t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefrombinding.tdx.Texture_f32_0_0_0_1t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <2 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefrombinding.tdx.Texture_v2f32_0_0_0_6t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <3 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v3f32_0_0_0_2t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_7t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_4t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_5t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_0_9t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplebias.f32.tdx.Texture_f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", float, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
declare <2 x float> @llvm.dx.resource.samplebias.clamp.v2f32.tdx.Texture_v2f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <2 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32, float)
declare <3 x float> @llvm.dx.resource.samplebias.v3f32.tdx.Texture_v3f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <3 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>, float)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, float, <3 x i32>)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32, float)
