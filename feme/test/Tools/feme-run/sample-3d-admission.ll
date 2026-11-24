; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Even valid constant-offset 3D implicit samples require a fragment stage.
; CHECK: requires heap-backed FP32 3D fragment implicit sampling with a default sampler, shape-matched coordinates and constant signed XYZ offsets
define void @main() #0 {
  %t = call target("dx.Texture", float, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_4t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %v = call float @llvm.dx.resource.sample.f32.tdx.Texture_f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", float, 0, 0, 0, 4) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, <3 x i32> zeroinitializer)
  call void @consume(float %v)
  ret void
}
declare target("dx.Texture", float, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_4t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare float @llvm.dx.resource.sample.f32.tdx.Texture_f32_0_0_0_4t.tdx.Sampler_0t.v3f32.v3i32(target("dx.Texture", float, 0, 0, 0, 4), target("dx.Sampler", 0), <3 x float>, <3 x i32>)
declare void @consume(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
