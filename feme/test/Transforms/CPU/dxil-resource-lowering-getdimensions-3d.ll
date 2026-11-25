; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <4 x i32> @writable_levels()
; CHECK: call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz
; CHECK-NOT: @feme.cpu.image.getdimensions
; CHECK-LABEL: define i32 @base_width(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.3d.v3i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 0, i1 true)
; CHECK: [[W:%.*]] = extractelement <3 x i32> [[S]], i64 0
; CHECK: ret i32 [[W]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x i32> @base_pair(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.3d.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: shufflevector <3 x i32> [[S]], <3 x i32> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @base_depth(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.3d.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: extractelement <3 x i32> [[S]], i32 2
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <4 x i32> @full(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.3d.v3i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 %mip, i1 true)
; CHECK: [[L:%.*]] = call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 %index)
; CHECK: [[V:%.*]] = shufflevector <3 x i32> [[S]], <3 x i32> poison,
; CHECK: [[R:%.*]] = insertelement <4 x i32> [[V]], i32 [[L]], i64 3
; CHECK: ret <4 x i32> [[R]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @count_only(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call <3 x i32> @feme.cpu.image.getdimensions.lod.3d.v3i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 0, i1 true)
; CHECK: [[L:%.*]] = call i32 @feme.cpu.image.querylevels.i32(ptr %image_heap, i32 %image_heap_count, i32 0)
; CHECK: insertelement <4 x i32> {{.*}}, i32 [[L]], i64 3
; CHECK: extractelement <4 x i32> {{.*}}, i32 3
; CHECK-NOT: call target("dx.

define i32 @base_width(i32 %index) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call i32 @llvm.dx.resource.getdimensions.x(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t)
  ret i32 %d
}
define <2 x i32> @base_pair() {
  %t = call target("dx.Texture", i32, 1, 0, 0, 4) @llvm.dx.resource.handlefromheap.u(i32 1)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy(target("dx.Texture", i32, 1, 0, 0, 4) %t)
  ret <2 x i32> %d
}
define i32 @base_depth() #0 {
  %t = call target("dx.Texture", i32, 1, 0, 0, 4) @llvm.dx.resource.handlefromheap.u(i32 1)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.Texture", i32, 1, 0, 0, 4) %t)
  %depth = extractelement <3 x i32> %d, i32 2
  ret i32 %depth
}
define <4 x i32> @full(i32 %index, i32 %mip) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, i32 %mip)
  ret <4 x i32> %d
}
define i32 @count_only() #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.f(i32 0)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, i32 0)
  %count = extractelement <4 x i32> %d, i32 3
  ret i32 %count
}
define <4 x i32> @writable_levels() {
  %t = call target("dx.Texture", i32, 1, 0, 0, 4) @llvm.dx.resource.handlefromheap.u(i32 1)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.levels.xyz(target("dx.Texture", i32, 1, 0, 0, 4) %t, i32 0)
  ret <4 x i32> %d
}
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.Texture", i32, 1, 0, 0, 4) @llvm.dx.resource.handlefromheap.u(i32)
attributes #0 = { "feme.shader.stage"="fragment" }
