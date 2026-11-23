; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s --implicit-check-not='call {{.*}}@llvm.dx.resource.samplebias'
; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,feme-dxil-raise-ops,verify -S %s | FileCheck %s --implicit-check-not='call {{.*}}@llvm.dx.resource.samplebias'
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Typed-to-legacy bridges isolate operand/result guards. Real DXIL handle
; creation is exercised by the forward roundtrip and native SDK fixtures.
%dx.types.Handle = type { ptr }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%bad.ret = type { float, float, float, i32, i32 }
%dx.types.ResRet.f16 = type { half, half, half, half, i32 }

; CHECK-LABEL: define float @poison_clamp(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.v4f32
; CHECK-SAME: float %bias, <2 x i32> <i32 1, i32 -2>)
; CHECK: extractelement <4 x float> {{.*}}, i64 3
; CHECK: ret float
define float @poison_clamp(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, float %bias) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 1, i32 -2, i32 undef, float %bias, float poison)
  %v = extractvalue %dx.types.ResRet.f32 %r, 3
  ret float %v
}
; CHECK-LABEL: define float @undef_clamp(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.v4f32
; CHECK-SAME: float -1.000000e+00, <2 x i32> zeroinitializer)
; CHECK: ret float
define float @undef_clamp(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float -1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @zero_clamp(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32
; CHECK-SAME: float 1.000000e+00, <2 x i32> zeroinitializer, float 0.000000e+00)
; CHECK: ret float
define float @zero_clamp(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float 0.0)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @dynamic_operands(
; CHECK: call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32
; CHECK-SAME: float %bias, <2 x i32> {{.*}}, float %clamp)
; CHECK: ret float
define float @dynamic_operands(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, float %bias, float %clamp, i32 %offset) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 %offset, i32 0, i32 undef, float %bias, float %clamp)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define i32 @mixed_status(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.f32
; CHECK: extractvalue %dx.types.ResRet.f32 {{.*}}, 0
; CHECK: extractvalue %dx.types.ResRet.f32 {{.*}}, 4
; CHECK: ret i32
define i32 @mixed_status(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  call void @consume(float %v)
  %status = extractvalue %dx.types.ResRet.f32 %r, 4
  ret i32 %status
}
; CHECK-LABEL: define %dx.types.ResRet.f32 @aggregate(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.f32
; CHECK: ret %dx.types.ResRet.f32
define %dx.types.ResRet.f32 @aggregate(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  ret %dx.types.ResRet.f32 %r
}
; CHECK-LABEL: define float @extra_component(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.scalar
; CHECK: ret float
define float @extra_component(target("dx.Texture", float, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_f32_0_0_0_2t(target("dx.Texture", float, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.scalar(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 1
  ret float %v
}
; CHECK-LABEL: define float @comparison_sampler(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.comparison
; CHECK: ret float
define float @comparison_sampler(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 1) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_1t(target("dx.Sampler", 1) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.comparison(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @cube_offset(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.cube
; CHECK: ret float
define float @cube_offset(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_5t(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.cube(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float 1.0, float undef, i32 1, i32 undef, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @dynamic_padding(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.f32
; CHECK: ret float
define float @dynamic_padding(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, i32 %offset) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 %offset, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_bias(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.badbias
; CHECK: ret float
define float @bad_bias(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.badbias(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, i32 1, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_clamp(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.badclamp
; CHECK: ret float
define float @bad_clamp(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.badclamp(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, i32 0)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_coord(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.badcoord
; CHECK: ret float
define float @bad_coord(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.badcoord(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, i32 0, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_offset(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.badoffset
; CHECK: ret float
define float @bad_offset(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.badoffset(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i64 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_result(
; CHECK: call %bad.ret @dx.op.sampleBias.badresult
; CHECK: ret float
define float @bad_result(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %bad.ret @dx.op.sampleBias.badresult(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %bad.ret %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_arity(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.badarity
; CHECK: ret float
define float @bad_arity(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.badarity(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define half @half_result(
; CHECK: call %dx.types.ResRet.f16 @dx.op.sampleBias.f16
; CHECK: ret half
define half @half_result(target("dx.Texture", <4 x half>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f16_0_0_0_2t(target("dx.Texture", <4 x half>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f16 @dx.op.sampleBias.f16(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f16 %r, 0
  ret half %v
}
; CHECK-LABEL: define float @integer_texel(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.integer
; CHECK: ret float
define float @integer_texel(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4i32_0_0_0_2t(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.integer(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @multisampled(
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleBias.multisampled
; CHECK: ret float
define float @multisampled(target("dx.Texture", <4 x float>, 0, 0, 0, 3) %t, target("dx.Sampler", 0) %s) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_3t(target("dx.Texture", <4 x float>, 0, 0, 0, 3) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %r = call %dx.types.ResRet.f32 @dx.op.sampleBias.multisampled(i32 61, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 1.0, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
declare void @consume(float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.scalar(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.comparison(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.cube(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.badbias(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, i32, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.badclamp(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, i32)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.badcoord(i32, %dx.types.Handle, %dx.types.Handle, i32, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.badoffset(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i64, i32, i32, float, float)
declare %bad.ret @dx.op.sampleBias.badresult(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.badarity(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float)
declare %dx.types.ResRet.f16 @dx.op.sampleBias.f16(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.integer(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleBias.multisampled(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_f32_0_0_0_2t(target("dx.Texture", float, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_1t(target("dx.Sampler", 1))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_5t(target("dx.Texture", <4 x float>, 0, 0, 0, 5))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f16_0_0_0_2t(target("dx.Texture", <4 x half>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4i32_0_0_0_2t(target("dx.Texture", <4 x i32>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_3t(target("dx.Texture", <4 x float>, 0, 0, 0, 3))
