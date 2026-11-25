; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; Unsupported shapes retain their original ABI, even in a mixed module.
; CHECK-LABEL: define <2 x i32> @array()
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.xy
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define i32 @nonheap(target("dx.Texture", float, 0, 0, 0, 2) %t)
; CHECK: call i32 @llvm.dx.resource.getdimensions.x
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define i32 @half_texel()
; CHECK: call i32 @llvm.dx.resource.getdimensions.x
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define <2 x i32> @pair(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[P:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i1 true)
; CHECK: ret <2 x i32> [[P]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @width(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[D:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 1, i1 true)
; CHECK: [[W:%.*]] = extractelement <2 x i32> [[D]], i64 0
; CHECK: ret i32 [[W]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define float @query_and_load(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[Q:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 2, i1 true)
; CHECK: [[H:%.*]] = extractelement <2 x i32> [[Q]], i32 1
; CHECK: [[C:%.*]] = insertelement <2 x i32> zeroinitializer, i32 [[H]], i32 1
; CHECK: [[X:%.*]] = extractelement <2 x i32> [[C]], i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], 0
; CHECK: [[Y:%.*]] = extractelement <2 x i32> [[C]], i64 1
; CHECK: [[OY:%.*]] = add i32 [[Y]], 0
; CHECK: call <4 x float> @feme.cpu.image.load.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 [[OX]], i32 [[OY]], i32 0, i32 0, i1 true)
; CHECK-NOT: call target("dx.

define <2 x i32> @pair(i32 %index) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.f(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  ret <2 x i32> %d
}
define i32 @width() {
  %t = call target("dx.Texture", <2 x i32>, 1, 0, 0, 2) @llvm.dx.resource.handlefromheap.u(i32 1)
  %d = call i32 @llvm.dx.resource.getdimensions.x.u(target("dx.Texture", <2 x i32>, 1, 0, 0, 2) %t)
  ret i32 %d
}
define float @query_and_load() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.f(i32 2)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.f(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t)
  %h = extractelement <2 x i32> %d, i32 1
  %c = insertelement <2 x i32> zeroinitializer, i32 %h, i32 1
  %v = call float @llvm.dx.resource.load.level.f32.f.v2i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2) %t, <2 x i32> %c, i32 0, <2 x i32> zeroinitializer)
  ret float %v
}
define <2 x i32> @array() {
  %t = call target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.a(i32 0)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.a(target("dx.Texture", float, 0, 0, 0, 7) %t)
  ret <2 x i32> %d
}
define i32 @nonheap(target("dx.Texture", float, 0, 0, 0, 2) %t) {
  %d = call i32 @llvm.dx.resource.getdimensions.x.s(target("dx.Texture", float, 0, 0, 0, 2) %t)
  ret i32 %d
}
define i32 @half_texel() {
  %t = call target("dx.Texture", half, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.h(i32 0)
  %d = call i32 @llvm.dx.resource.getdimensions.x.h(target("dx.Texture", half, 0, 0, 0, 2) %t)
  ret i32 %d
}

declare target("dx.Texture", <4 x float>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.Texture", <2 x i32>, 1, 0, 0, 2) @llvm.dx.resource.handlefromheap.u(i32)
declare target("dx.Texture", float, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.a(i32)
declare target("dx.Texture", half, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.h(i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.f(target("dx.Texture", <4 x float>, 0, 0, 0, 2))
declare i32 @llvm.dx.resource.getdimensions.x.u(target("dx.Texture", <2 x i32>, 1, 0, 0, 2))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.a(target("dx.Texture", float, 0, 0, 0, 7))
declare i32 @llvm.dx.resource.getdimensions.x.s(target("dx.Texture", float, 0, 0, 0, 2))
declare i32 @llvm.dx.resource.getdimensions.x.h(target("dx.Texture", half, 0, 0, 0, 2))
declare float @llvm.dx.resource.load.level.f32.f.v2i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
