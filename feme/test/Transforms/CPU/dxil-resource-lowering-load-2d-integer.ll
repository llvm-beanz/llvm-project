; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative --implicit-check-not=sitofp --implicit-check-not=uitofp
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; CHECK-LABEL: define float @mixed_result(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define <4 x i32> @too_wide(
; CHECK: call <4 x i32> @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define i32 @non_heap(
; CHECK: call i32 @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define i32 @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <2 x i32> %coord, i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], -1
; CHECK: [[Y:%.*]] = extractelement <2 x i32> %coord, i64 1
; CHECK: [[OY:%.*]] = add i32 [[Y]], 1
; CHECK: [[P:%.*]] = call <4 x i32> @feme.cpu.image.load.2d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 %image, i32 [[OX]], i32 [[OY]], i32 %mip, i32 0, i1 true)
; CHECK: extractelement <4 x i32> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x i32> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[AX:%.*]] = extractelement <3 x i32> %coord, i64 0
; CHECK: [[AOX:%.*]] = add i32 [[AX]], 1
; CHECK: [[AY:%.*]] = extractelement <3 x i32> %coord, i64 1
; CHECK: [[AOY:%.*]] = add i32 [[AY]], -1
; CHECK: [[L:%.*]] = extractelement <3 x i32> %coord, i64 2
; CHECK: [[A:%.*]] = call <4 x i32> @feme.cpu.image.load.2darray.v4i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 [[AOX]], i32 [[AOY]], i32 [[L]], i32 %mip, i32 0, i1 true)
; CHECK: shufflevector <4 x i32> [[A]], <4 x i32> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define i32 @plain(i32 %image, <2 x i32> %coord, i32 %mip) {
  %t = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4i32_0_0_0_2t(i32 %image)
  %r = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_v4i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t, <2 x i32> %coord, i32 %mip, <2 x i32> <i32 -1, i32 1>)
  ret i32 %r
}
define <2 x i32> @array(<3 x i32> %coord, i32 %mip) #0 {
  %t = call target("dx.Texture", <4 x i32>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4i32_0_0_0_7t(i32 0)
  %r = call <2 x i32> @llvm.dx.resource.load.level.v2i32.tdx.Texture_v4i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 7) %t, <3 x i32> %coord, i32 %mip, <2 x i32> <i32 1, i32 -1>)
  ret <2 x i32> %r
}
define float @mixed_result() {
  %t = call target("dx.Texture", <4 x i32>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4i32_0_0_0_2t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t, <2 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret float %r
}
define <4 x i32> @too_wide() {
  %t = call target("dx.Texture", i32, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_7t(i32 0)
  %r = call <4 x i32> @llvm.dx.resource.load.level.v4i32.tdx.Texture_i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 7) %t, <3 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret <4 x i32> %r
}
define i32 @non_heap(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t) {
  %r = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_v4i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 2) %t, <2 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret i32 %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 false
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 false
declare target("dx.Texture", <4 x i32>, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_v4i32_0_0_0_2t(i32)
declare target("dx.Texture", <4 x i32>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_v4i32_0_0_0_7t(i32)
declare target("dx.Texture", i32, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_7t(i32)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_v4i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
declare <2 x i32> @llvm.dx.resource.load.level.v2i32.tdx.Texture_v4i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 7), <3 x i32>, i32, <2 x i32>)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
declare <4 x i32> @llvm.dx.resource.load.level.v4i32.tdx.Texture_i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 7), <3 x i32>, i32, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
