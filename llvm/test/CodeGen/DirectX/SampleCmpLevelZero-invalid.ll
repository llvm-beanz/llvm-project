; RUN: not opt -S -dxil-op-lower %s 2>&1 | FileCheck %s
; CHECK: SampleCmpLevelZero requires a scalar float result
; CHECK: SampleCmpLevelZero requires a comparison sampler

target triple = "dxil-pc-shadermodel6.6-compute"

define <4 x float> @invalid_result(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord) {
  %v = call <4 x float> @llvm.dx.resource.samplecmplevelzero.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 1) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
  ret <4 x float> %v
}
declare <4 x float> @llvm.dx.resource.samplecmplevelzero.v4f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)

define float @invalid_sampler(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord) {
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 0) %s, <2 x float> %coord, float 0.5, <2 x i32> zeroinitializer)
  ret float %v
}
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
