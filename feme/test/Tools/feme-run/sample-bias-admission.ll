; RUN: split-file %s %t
; RUN: not feme-run %t/plain.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/plain.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/plain.ll -o %t/plain.bc
; RUN: not feme --target=%feme_host_triple %t/plain.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/clamp.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/clamp.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/clamp.ll -o %t/clamp.bc
; RUN: not feme --target=%feme_host_triple %t/clamp.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK: requires heap-backed FP32 Texture2D fragment biased sampling with a default sampler, shape-matched coordinates and constant signed offsets
;--- plain.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float 1.0, <2 x i32> zeroinitializer)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- clamp.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float -1.0, <2 x i32> zeroinitializer, float 0.0)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_2t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_2t.tdx.Sampler_0t.v2f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), target("dx.Sampler", 0), <2 x float>, float, <2 x i32>, float)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
