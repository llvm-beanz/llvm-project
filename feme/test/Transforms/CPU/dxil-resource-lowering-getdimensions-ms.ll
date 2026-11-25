; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define <3 x i32> @levels()
; CHECK: call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy
; CHECK-NOT: @feme.cpu.image.
; CHECK-LABEL: define i32 @width(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i1 true)
; CHECK: extractelement <2 x i32> [[S]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x i32> @array_pair(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: shufflevector <3 x i32> [[S]], <3 x i32> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @array_layers(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: extractelement <3 x i32> [[S]], i32 2
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <3 x i32> @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <2 x i32> @feme.cpu.image.getdimensions.2d.v2i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i1 true)
; CHECK: [[V:%.*]] = shufflevector <2 x i32> [[S]], <2 x i32> poison,
; CHECK: [[C:%.*]] = call i32 @feme.cpu.image.querysamples.i32(ptr %image_heap, i32 %image_heap_count, i32 %index)
; CHECK: [[R:%.*]] = insertelement <3 x i32> [[V]], i32 [[C]], i64 2
; CHECK: ret <3 x i32> [[R]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <4 x i32> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[S:%.*]] = call <3 x i32> @feme.cpu.image.getdimensions.lod.2darray.v3i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 0, i1 true)
; CHECK: [[V:%.*]] = shufflevector <3 x i32> [[S]], <3 x i32> poison,
; CHECK: [[C:%.*]] = call i32 @feme.cpu.image.querysamples.i32(ptr %image_heap, i32 %image_heap_count, i32 1)
; CHECK: [[R:%.*]] = insertelement <4 x i32> [[V]], i32 [[C]], i64 3
; CHECK: ret <4 x i32> [[R]]
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define i32 @samples_only(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: call i32 @feme.cpu.image.querysamples.i32
; CHECK: extractelement <3 x i32> {{.*}}, i32 2
; CHECK-NOT: call target("dx.

define i32 @width(i32 %index) {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call i32 @llvm.dx.resource.getdimensions.x(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t)
  ret i32 %d
}
define <2 x i32> @array_pair() {
  %t = call target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefromheap.i(i32 1)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy(target("dx.MSTexture", i32, 0, 2, 1, 8) %t)
  ret <2 x i32> %d
}
define i32 @array_layers() #0 {
  %t = call target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefromheap.i(i32 1)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.xyz(target("dx.MSTexture", i32, 0, 2, 1, 8) %t)
  %layers = extractelement <3 x i32> %d, i32 2
  ret i32 %layers
}
define <3 x i32> @plain(i32 %index) {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.f(i32 %index)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t)
  ret <3 x i32> %d
}
define <4 x i32> @array() {
  %t = call target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefromheap.i(i32 1)
  %d = call <4 x i32> @llvm.dx.resource.getdimensions.samples.xyz(target("dx.MSTexture", i32, 0, 2, 1, 8) %t)
  ret <4 x i32> %d
}
define i32 @samples_only() #0 {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.f(i32 0)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.samples.xy(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t)
  %samples = extractelement <3 x i32> %d, i32 2
  ret i32 %samples
}
define <3 x i32> @levels() {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.f(i32 0)
  %d = call <3 x i32> @llvm.dx.resource.getdimensions.levels.xy(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t, i32 0)
  ret <3 x i32> %d
}
declare target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.f(i32)
declare target("dx.MSTexture", i32, 0, 2, 1, 8) @llvm.dx.resource.handlefromheap.i(i32)
attributes #0 = { "feme.shader.stage"="fragment" }
