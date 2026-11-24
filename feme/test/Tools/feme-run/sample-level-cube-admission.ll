; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; A zero vector offset cannot bypass the shared cube matcher via legacy lowering.
; CHECK: requires heap-backed FP32 cube/array explicit-LOD sampling with a default sampler, shape-matched coordinates and unused (zero/undef/poison) scalar offsets
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %v = call <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> <float 1.0, float 0.0, float 0.0>, float 1.0, <2 x i32> zeroinitializer)
  call void @consume(<4 x float> %v)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplelevel.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
