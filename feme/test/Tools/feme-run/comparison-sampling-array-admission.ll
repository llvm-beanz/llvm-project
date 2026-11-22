; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; The array layer cannot be omitted, even though a plain 2D comparison
; with otherwise identical handles/coordinates is supported.
; CHECK: requires scalar FP32 1D/2D/cube comparison sampling with heap handles, shape-matched coordinates and supported offsets
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7) %t, target("dx.Sampler", 1) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer)
  call void @consume(float %v)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_7t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_7t.tdx.Sampler_1t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 7), target("dx.Sampler", 1), <2 x float>, float, <2 x i32>)
declare void @consume(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
