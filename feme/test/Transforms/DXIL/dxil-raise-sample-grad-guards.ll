; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s --implicit-check-not='call {{.*}}@llvm.dx.resource.samplegrad'
; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,feme-dxil-raise-ops,verify -S %s | FileCheck %s --implicit-check-not='call {{.*}}@llvm.dx.resource.samplegrad'
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
%dx.types.Handle = type { ptr }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.f16 = type { half, half, half, half, i32 }
%bad.ret = type { float, float, float, i32, i32 }

; CHECK-LABEL: define float @clamps_and_dynamic_operands(
; CHECK: call <4 x float> @llvm.dx.resource.samplegrad.v4f32
; CHECK-SAME: <2 x float> <float 5.000000e-01, float 2.500000e-01>, <2 x float> <float -2.500000e-01, float 5.000000e-01>, <2 x i32> <i32 1, i32 -2>)
; CHECK: call <4 x float> @llvm.dx.resource.samplegrad.v4f32
; CHECK: call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32
; CHECK-SAME: float 0.000000e+00)
; CHECK-DAG: insertelement <2 x float> poison, float %dx, i32 0
; CHECK-DAG: insertelement <2 x float> {{.*}}, float %dy, i32 1
; CHECK-DAG: insertelement <2 x i32> poison, i32 %offset, i32 0
; CHECK: call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32
; CHECK-SAME: float %clamp)
; CHECK: ret float
define float @clamps_and_dynamic_operands(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, float %dx, float %dy, i32 %offset, float %clamp) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %p = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 1, i32 -2, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float poison)
  %u = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %z = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float 0.0)
  %d = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 %offset, i32 -1, i32 0, float %dx, float %dy, float 0.0, float %dy, float %dx, float poison, float %clamp)
  %pv = extractvalue %dx.types.ResRet.f32 %p, 3
  %uv = extractvalue %dx.types.ResRet.f32 %u, 2
  %zv = extractvalue %dx.types.ResRet.f32 %z, 1
  %dv = extractvalue %dx.types.ResRet.f32 %d, 0
  %a = fadd float %pv, %uv
  %b = fadd float %zv, %dv
  %v = fadd float %a, %b
  ret float %v
}

; CHECK-LABEL: define void @rejected(
; CHECK-COUNT-18: call {{.*}}@dx.op.sampleGrad
; CHECK: ret void
define void @rejected(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Texture", float, 0, 0, 0, 2) %scalar, target("dx.Texture", <4 x float>, 0, 0, 0, 5) %cube, target("dx.Texture", <4 x float>, 0, 0, 0, 6) %array, target("dx.Texture", <4 x float>, 0, 0, 0, 3) %ms, target("dx.Texture", <4 x half>, 0, 0, 0, 2) %half, target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %integer, target("dx.Sampler", 0) %s, target("dx.Sampler", 1) %cmp, float %grad, i32 %offset) {
  %th = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %sc = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_f32_0_0_0_2t(target("dx.Texture", float, 0, 0, 0, 2) %scalar)
  %cu = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_5t(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %cube)
  %ar = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_6t(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %array)
  %mh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_3t(target("dx.Texture", <4 x float>, 0, 0, 0, 3) %ms)
  %hh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f16_0_0_0_2t(target("dx.Texture", <4 x half>, 0, 0, 0, 2) %half)
  %ih = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4i32_0_0_0_2t(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %integer)
  %sh = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0) %s)
  %ch = call %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_1t(target("dx.Sampler", 1) %cmp)
  ; Mixed value/status and aggregate users must not introduce another sample.
  %status = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %value = extractvalue %dx.types.ResRet.f32 %status, 0
  %code = extractvalue %dx.types.ResRet.f32 %status, 4
  call void @consume(float %value, i32 %code)
  %aggregate = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  call void @consume.aggregate(%dx.types.ResRet.f32 %aggregate)
  %wide = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %sc, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %extra = extractvalue %dx.types.ResRet.f32 %wide, 1
  call void @consume(float %extra, i32 0)
  %comparison = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %ch, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %cubeoffset = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %cu, %dx.types.Handle %sh, float 0.25, float 0.75, float 1.0, float undef, i32 1, i32 0, i32 0, float 0.5, float 0.25, float 0.125, float -0.25, float 0.5, float 0.75, float undef)
  %layerdx = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %ar, %dx.types.Handle %sh, float 0.25, float 1.0, float undef, float undef, i32 0, i32 undef, i32 undef, float 0.5, float 1.0, float undef, float 0.25, float undef, float undef, float undef)
  %layerdy = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %ar, %dx.types.Handle %sh, float 0.25, float 1.0, float undef, float undef, i32 0, i32 undef, i32 undef, float 0.5, float undef, float undef, float 0.25, float %grad, float undef, float undef)
  %unusedoffset = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 %offset, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %badx = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, i32 1, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %bady = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, i32 1, float 0.5, float undef, float undef)
  %badclamp = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, i32 0)
  %badcoord = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, i32 0, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %badoffset = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, float undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %badresult = call %bad.ret (...) @dx.op.sampleGrad.badresult(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %badarity = call %dx.types.ResRet.f32 (...) @dx.op.sampleGrad.malformed(i32 63, %dx.types.Handle %th, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef)
  %inttexel = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %ih, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %multisampled = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %mh, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  %halftexel = call %dx.types.ResRet.f16 (...) @dx.op.sampleGrad.f16(i32 63, %dx.types.Handle %hh, %dx.types.Handle %sh, float 0.25, float 0.75, float undef, float undef, i32 0, i32 0, i32 undef, float 0.5, float 0.25, float undef, float -0.25, float 0.5, float undef, float undef)
  ret void
}
declare void @consume(float, i32)
declare void @consume.aggregate(%dx.types.ResRet.f32)
declare %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float, float, float, float, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleGrad.malformed(...)
declare %bad.ret @dx.op.sampleGrad.badresult(...)
declare %dx.types.ResRet.f16 @dx.op.sampleGrad.f16(...)
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_2t(target("dx.Texture", <4 x float>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_f32_0_0_0_2t(target("dx.Texture", float, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_5t(target("dx.Texture", <4 x float>, 0, 0, 0, 5))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_6t(target("dx.Texture", <4 x float>, 0, 0, 0, 6))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f32_0_0_0_3t(target("dx.Texture", <4 x float>, 0, 0, 0, 3))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4f16_0_0_0_2t(target("dx.Texture", <4 x half>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Texture_v4i32_0_0_0_2t(target("dx.Texture", <4 x i32>, 0, 0, 0, 2))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_0t(target("dx.Sampler", 0))
declare %dx.types.Handle @llvm.dx.resource.casthandle.s_dx.types.Handles.tdx.Sampler_1t(target("dx.Sampler", 1))
