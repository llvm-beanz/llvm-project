; RUN: not feme-run %s 2>&1 | FileCheck %s
; RUN: not feme-run --reference %s 2>&1 | FileCheck %s
; RUN: llvm-as %s -o %t.bc
; RUN: not feme --target=%feme_host_triple %t.bc -o %t.o 2>&1 | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK: requires heap-backed FP32/i32 2D/array texel loading with paired/three-component i32 coordinates, an i32 mip and constant signed XY offsets
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = insertelement <2 x i32> zeroinitializer, i32 %tid, i32 0
  %t = call target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_7t(i32 0)
  %v = call float @llvm.dx.resource.load.level.f32.tdx.Texture_f32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", float, 0, 0, 0, 7) %t, <3 x i32> zeroinitializer, i32 0, <2 x i32> %offset)
  call void @consume(float %v)
  ret void
}
declare target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_7t(i32)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_f32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", float, 0, 0, 0, 7), <3 x i32>, i32, <2 x i32>)
declare i32 @llvm.dx.thread.id(i32)
declare void @consume(float)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
