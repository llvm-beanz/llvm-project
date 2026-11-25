; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <3 x i32> @writable()
; CHECK: call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define <3 x i32> @full(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.lod.1darray.v2i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 %mip, i1 true)
; CHECK: [[L:%.*]] = call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 %index)
; CHECK: [[V:%.*]] = shufflevector <2 x i32> [[S]], <2 x i32> poison,
; CHECK: [[R:%.*]] = insertelement <3 x i32> [[V]], i32 [[L]], i64 2
; CHECK: ret <3 x i32> [[R]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @layers_only(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call <2 x i32> @feme.cpu.image.getdimensions.lod.1darray.v2i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 2, i1 true)
; CHECK: call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 1)
; CHECK: extractelement <3 x i32> {{.*}}, i32 1
; CHECK-NOT: call target("dx.

define <3 x i32> @full(i32 %index, i32 %mip) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.f(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, i32 %mip)
  ret <3 x i32> %d
}
define i32 @layers_only() #0 {
  %t = call target("dx.Texture", i32, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.i(i32 1)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.i(target("dx.Texture", i32, 0, 0, 0, 6) %t, i32 2)
  %layers = extractelement <3 x i32> %d, i32 1
  ret i32 %layers
}
define <3 x i32> @writable() {
  %t = call target("dx.Texture", float, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.u(i32 0)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.u(target("dx.Texture", float, 1, 0, 0, 6) %t, i32 0)
  ret <3 x i32> %d
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.Texture", i32, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.i(i32)
declare target("dx.Texture", float, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.u(i32)
declare <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.f(target("dx.Texture", <4 x float>, 0, 0, 0, 6), i32)
declare <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.i(target("dx.Texture", i32, 0, 0, 0, 6), i32)
declare <3 x i32> @llvm.dx.resource.getdimensions.levels.xy.u(target("dx.Texture", float, 1, 0, 0, 6), i32)
attributes #0 = { "feme.shader.stage"="fragment" }
