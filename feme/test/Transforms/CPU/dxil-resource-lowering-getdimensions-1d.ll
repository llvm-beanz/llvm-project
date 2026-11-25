; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <2 x i32> @plain_pair()
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.xy
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define i32 @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[P:%.*]] = call i32 @feme.cpu.image.getdimensions.lod.1d.i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 0, i1 true)
; CHECK: ret i32 [[P]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x i32> @array_pair(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[A:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.lod.1darray.v2i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: ret <2 x i32> [[A]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @array_width(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[D:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.lod.1darray.v2i32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 0, i1 true)
; CHECK: [[W:%.*]] = extractelement <2 x i32> [[D]], i64 0
; CHECK: ret i32 [[W]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @layers_and_load(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[Q:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.lod.1darray.v2i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: [[L:%.*]] = extractelement <2 x i32> [[Q]], i32 1
; CHECK: [[LAST:%.*]] = sub i32 [[L]], 1
; CHECK: [[C:%.*]] = insertelement <2 x i32> zeroinitializer, i32 [[LAST]], i32 1
; CHECK: [[X:%.*]] = extractelement <2 x i32> [[C]], i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], 0
; CHECK: [[LAYER:%.*]] = extractelement <2 x i32> [[C]], i64 1
; CHECK: call <4 x i32> @feme.cpu.image.load.1darray.v4i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 [[OX]], i32 [[LAYER]], i32 0, i1 true)
; CHECK-NOT: call target("dx.

define i32 @plain(i32 %index) {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.p(i32 %index)
  %d = call i32 @llvm.dx.resource.getdimensions.x.p(target("dx.Texture", float, 0, 0, 0, 1) %t)
  ret i32 %d
}
define <2 x i32> @array_pair() {
  %t = call target("dx.Texture", <2 x i32>, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.a(i32 1)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.a(target("dx.Texture", <2 x i32>, 1, 0, 0, 6) %t)
  ret <2 x i32> %d
}
define i32 @array_width() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.f(i32 2)
  %d = call i32 @llvm.dx.resource.getdimensions.x.f(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t)
  ret i32 %d
}
define i32 @layers_and_load() #0 {
  %t = call target("dx.Texture", <2 x i32>, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.a(i32 1)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.a(target("dx.Texture", <2 x i32>, 1, 0, 0, 6) %t)
  %layers = extractelement <2 x i32> %d, i32 1
  %last = sub i32 %layers, 1
  %c = insertelement <2 x i32> zeroinitializer, i32 %last, i32 1
  %v = call i32 @llvm.dx.resource.load.level.i32.a.v2i32.i32.i32(target("dx.Texture", <2 x i32>, 1, 0, 0, 6) %t, <2 x i32> %c, i32 0, i32 0)
  ret i32 %v
}
define <2 x i32> @plain_pair() {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.p(i32 0)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.p(target("dx.Texture", float, 0, 0, 0, 1) %t)
  ret <2 x i32> %d
}

declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.p(i32)
declare target("dx.Texture", <2 x i32>, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.a(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.f(i32)
declare i32 @llvm.dx.resource.getdimensions.x.p(target("dx.Texture", float, 0, 0, 0, 1))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.p(target("dx.Texture", float, 0, 0, 0, 1))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.a(target("dx.Texture", <2 x i32>, 1, 0, 0, 6))
declare i32 @llvm.dx.resource.getdimensions.x.f(target("dx.Texture", <4 x float>, 0, 0, 0, 6))
declare i32 @llvm.dx.resource.load.level.i32.a.v2i32.i32.i32(target("dx.Texture", <2 x i32>, 1, 0, 0, 6), <2 x i32>, i32, i32)
attributes #0 = { "feme.shader.stage"="fragment" }
