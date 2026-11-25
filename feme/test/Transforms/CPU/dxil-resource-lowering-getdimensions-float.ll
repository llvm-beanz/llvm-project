; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; Dimension helpers return integer bits. Preserve the caller's conversion:
; sitofp and uitofp are not interchangeable above the signed-i32 boundary.
; CHECK-LABEL: define <2 x float> @width_conversions(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[D:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i1 true)
; CHECK-NEXT: [[W:%.*]] = extractelement <2 x i32> [[D]], i64 0
; CHECK-NEXT: [[S:%.*]] = sitofp i32 [[W]] to float
; CHECK-NEXT: [[U:%.*]] = uitofp i32 [[W]] to float
; CHECK-NEXT: [[V:%.*]] = insertelement <2 x float> poison, float [[S]], i32 0
; CHECK-NEXT: [[R:%.*]] = insertelement <2 x float> [[V]], float [[U]], i32 1
; CHECK-NEXT: ret <2 x float> [[R]]
; CHECK-NOT: call target("dx.
define <2 x float> @width_conversions(i32 %index) {
  %t = call target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call i32 @llvm.dx.resource.getdimensions.x(target("dx.Texture", float, 0, 0, 0, 2) %t)
  %s = sitofp i32 %d to float
  %u = uitofp i32 %d to float
  %v = insertelement <2 x float> poison, float %s, i32 0
  %r = insertelement <2 x float> %v, float %u, i32 1
  ret <2 x float> %r
}

; CHECK-LABEL: define <4 x float> @array_sample_conversions(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[D:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: [[V:%.*]] = shufflevector <3 x i32> [[D]], <3 x i32> poison,
; CHECK: [[S:%.*]] = call i32 @feme.cpu.image.querysamples.i32(ptr %image_heap, i32 %image_heap_count, i32 1)
; CHECK-NEXT: [[R:%.*]] = insertelement <4 x i32> [[V]], i32 [[S]], i64 3
; CHECK-NEXT: [[F:%.*]] = uitofp <4 x i32> [[R]] to <4 x float>
; CHECK-NEXT: ret <4 x float> [[F]]
; CHECK-NOT: call target("dx.
define <4 x float> @array_sample_conversions() #0 {
  %t = call target("dx.MSTexture", i32, 0, 2, 0, 8) @llvm.dx.resource.handlefromheap.ms(i32 1)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.samples.xyz(target("dx.MSTexture", i32, 0, 2, 0, 8) %t)
  %f = uitofp <4 x i32> %d to <4 x float>
  ret <4 x float> %f
}
declare target("dx.Texture", float, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.MSTexture", i32, 0, 2, 0, 8) @llvm.dx.resource.handlefromheap.ms(i32)
attributes #0 = { "feme.shader.stage"="fragment" }
