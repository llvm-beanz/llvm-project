; RUN: split-file %s %t
; RUN: not feme-run %t/plain.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/plain.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/plain.ll -o %t/plain.bc
; RUN: not feme --target=%feme_host_triple %t/plain.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/clamp.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/clamp.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/clamp.ll -o %t/clamp.bc
; RUN: not feme --target=%feme_host_triple %t/clamp.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/array.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/array.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/array.ll -o %t/array.bc
; RUN: not feme --target=%feme_host_triple %t/array.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/cube.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/cube.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/cube.ll -o %t/cube.bc
; RUN: not feme --target=%feme_host_triple %t/cube.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/cubearray.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/cubearray.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/cubearray.ll -o %t/cubearray.bc
; RUN: not feme --target=%feme_host_triple %t/cubearray.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/one.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/one.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/one.ll -o %t/one.bc
; RUN: not feme --target=%feme_host_triple %t/one.bc -o %t.o 2>&1 | FileCheck %s
; RUN: not feme-run %t/onearray.ll 2>&1 | FileCheck %s
; RUN: not feme-run --reference %t/onearray.ll 2>&1 | FileCheck %s
; RUN: llvm-as %t/onearray.ll -o %t/onearray.bc
; RUN: not feme --target=%feme_host_triple %t/onearray.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK: requires heap-backed FP32 1D/2D/cube (including array) fragment biased sampling with a default sampler, shape-matched coordinates and supported offsets
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
;--- array.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float -1.0, <2 x i32> zeroinitializer, float 0.0)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_7t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_7t.tdx.Sampler_0t.v3f32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 7), target("dx.Sampler", 0), <3 x float>, float, <2 x i32>, float)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- cube.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5) %t, target("dx.Sampler", 0) %s, <3 x float> zeroinitializer, float 1.0, i32 poison)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 5) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_5t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_5t.tdx.Sampler_0t.v3f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 5), target("dx.Sampler", 0), <3 x float>, float, i32)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- cubearray.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9) %t, target("dx.Sampler", 0) %s, <4 x float> zeroinitializer, float -1.0, i32 0, float 2.0)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 9) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_9t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_9t.tdx.Sampler_0t.v4f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 9), target("dx.Sampler", 0), <4 x float>, float, i32, float)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- one.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, target("dx.Sampler", 0) %s, float 0.5, float 1.0, i32 -1)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.v4f32.tdx.Texture_v4f32_0_0_0_1t.tdx.Sampler_0t.f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), target("dx.Sampler", 0), float, float, i32)
declare void @consume(<4 x float>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
;--- onearray.ll
define void @main() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %s = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, target("dx.Sampler", 0) %s, <2 x float> zeroinitializer, float -1.0, i32 1, float 0.0)
  call void @consume(<4 x float> %r)
  ret void
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap.tdx.Sampler_0t(i32)
declare <4 x float> @llvm.dx.resource.samplebias.clamp.v4f32.tdx.Texture_v4f32_0_0_0_6t.tdx.Sampler_0t.v2f32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), target("dx.Sampler", 0), <2 x float>, float, i32, float)
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
