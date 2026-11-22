; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; CHECK: requires scalar FP32 1D/2D comparison sampling with heap handles, shape-matched coordinates and constant i32 offsets
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 0.5, <2 x i32> zeroinitializer)
  call void @consume(float %v)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_2t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 1, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare void @consume(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }
