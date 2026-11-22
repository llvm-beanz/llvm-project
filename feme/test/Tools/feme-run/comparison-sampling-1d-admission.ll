; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Scalar 1D offsets must still be constant, not a runtime lane index.
; CHECK: requires scalar FP32 1D/2D comparison sampling with heap handles, shape-matched coordinates and constant i32 offsets
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32 0)
  %s = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32 0)
  %offset = call i32 @llvm.dx.thread.id(i32 0)
  %v = call float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1) %t, target("dx.Sampler", 1) %s, float 0.5, float 0.5, i32 %offset)
  call void @consume(float %v)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 1, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_1_1t(i32)
declare target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap.tdx.Sampler_1t(i32)
declare float @llvm.dx.resource.samplecmplevelzero.f32.tdx.Texture_v4f32_0_0_1_1t.tdx.Sampler_1t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 1, 1), target("dx.Sampler", 1), float, float, i32)
declare i32 @llvm.dx.thread.id(i32)
declare void @consume(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
