; RUN: opt -S -dxil-op-lower %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define float @comparison_2d(
define float @comparison_2d(<2 x float> %coord, float %reference) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32 7, i32 3, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 7, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: [[X:%.*]] = extractelement <2 x float> %coord, i32 0
  ; CHECK: [[Y:%.*]] = extractelement <2 x float> %coord, i32 1
  ; CHECK: [[R:%.*]] = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65,
  ; CHECK-SAME: float [[X]], float [[Y]], float undef, float undef, i32 1, i32 -2, i32 undef, float %reference)
  ; CHECK: [[V:%.*]] = extractvalue %dx.types.ResRet.f32 [[R]], 0
  ; CHECK: ret float [[V]]
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float %reference, <2 x i32> <i32 1, i32 -2>)
  ret float %v
}

; CHECK-LABEL: define float @comparison_cube_array(
define float @comparison_cube_array(<4 x float> %coord, float %reference) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_9t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65,
  ; CHECK-SAME: i32 poison, i32 undef, i32 undef, float %reference)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9) %t, target("dx.Sampler", 1) %s, <4 x float> %coord, float %reference, i32 poison)
  ret float %v
}

declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_2t(i32, i32, i32, i32, ptr)
declare target("dx.Texture", <4 x float>, 0, 0, 1, 9) @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_0_0_1_9t(i32, i32, i32, i32, ptr)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding.tdx.Sampler_1t(i32, i32, i32, i32, ptr)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_9t.tdx.Sampler_1t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 9), target("dx.Sampler", 1), <4 x float>, float, i32)
