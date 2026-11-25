; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <2 x i32> @writable()
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.levels.x
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define <2 x i32> @undefined_mip()
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.levels.x
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define <2 x i32> @full(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[W:%.*]] = call i32 @feme.cpu.image.getdimensions.lod.1d.i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 %mip, i1 true)
; CHECK: [[L:%.*]] = call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 %index)
; CHECK: [[V:%.*]] = insertelement <2 x i32> poison, i32 [[W]], i64 0
; CHECK: [[R:%.*]] = insertelement <2 x i32> [[V]], i32 [[L]], i64 1
; CHECK: ret <2 x i32> [[R]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @count_only(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call i32 @feme.cpu.image.getdimensions.lod.1d.i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: [[L:%.*]] = call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 1)
; CHECK: insertelement <2 x i32> {{.*}}, i32 [[L]], i64 1
; CHECK: extractelement <2 x i32> {{.*}}, i32 1
; CHECK-NOT: call target("dx.

define <2 x i32> @full(i32 %index, i32 %mip) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x.f(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 %mip)
  ret <2 x i32> %d
}
define i32 @count_only() #0 {
  %t = call target("dx.Texture", i32, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.i(i32 1)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x.i(target("dx.Texture", i32, 0, 0, 0, 1) %t, i32 0)
  %levels = extractelement <2 x i32> %d, i32 1
  ret i32 %levels
}
define <2 x i32> @writable() {
  %t = call target("dx.Texture", float, 1, 0, 0, 1) @llvm.dx.resource.handlefromheap.u(i32 0)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x.u(target("dx.Texture", float, 1, 0, 0, 1) %t, i32 0)
  ret <2 x i32> %d
}
define <2 x i32> @undefined_mip() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.f(i32 0)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.levels.x.f(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 poison)
  ret <2 x i32> %d
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.Texture", i32, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.i(i32)
declare target("dx.Texture", float, 1, 0, 0, 1) @llvm.dx.resource.handlefromheap.u(i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.levels.x.f(target("dx.Texture", <4 x float>, 0, 0, 0, 1), i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.levels.x.i(target("dx.Texture", i32, 0, 0, 0, 1), i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.levels.x.u(target("dx.Texture", float, 1, 0, 0, 1), i32)
attributes #0 = { "feme.shader.stage"="fragment" }
