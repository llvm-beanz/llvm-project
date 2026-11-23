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
; Valid 3D shapes still reject dynamic or partially undefined spatial offsets.
; CHECK: requires heap-backed FP32 1D/2D/3D/cube (including array) explicit-gradient sampling
; CHECK-SAME: constant signed 1D/2D/3D
;--- plain.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = insertelement <3 x i32> zeroinitializer, i32 %tid, i32 2
  %r = call <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> <float 0.5, float 0.25, float 0.125>, <3 x float> <float -0.25, float 0.5, float -0.125>, <3 x i32> %offset)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplegrad.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <3 x i32>)
declare void @consume(<4 x float>)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- clamp.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x float> <float 0.5, float 0.25, float 0.125>, <3 x float> <float -0.25, float 0.5, float -0.125>, <3 x i32> <i32 0, i32 0, i32 poison>, float 0.0)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplegrad.clamp.v4f32.tdx.Texture_v4f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3f32.v3f32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x float>, <3 x float>, <3 x i32>, float)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
