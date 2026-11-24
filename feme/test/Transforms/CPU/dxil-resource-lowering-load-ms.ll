; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @mixed_scalar(
; CHECK: call float @llvm.dx.resource.load.ms
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @bad_coord(
; CHECK: call float @llvm.dx.resource.load.ms
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define <4 x float> @too_wide(
; CHECK: call <4 x float> @llvm.dx.resource.load.ms
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <2 x i32> %coord, i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], -1
; CHECK: [[Y:%.*]] = extractelement <2 x i32> %coord, i64 1
; CHECK: [[OY:%.*]] = add i32 [[Y]], 1
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.load.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 %image, i32 [[OX]], i32 [[OY]], i32 0, i32 %sample, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <3 x i32> %coord, i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], 1
; CHECK: [[Y:%.*]] = extractelement <3 x i32> %coord, i64 1
; CHECK: [[OY:%.*]] = add i32 [[Y]], -1
; CHECK: [[L:%.*]] = extractelement <3 x i32> %coord, i64 2
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.load.2darray.v4f32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 [[OX]], i32 [[OY]], i32 [[L]], i32 0, i32 %sample, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, <2 x i32> %coord, i32 %sample) {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_v4f32_0_4_0_3t(i32 %image)
  %r = call float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_v4f32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t, <2 x i32> %coord, i32 %sample, <2 x i32> <i32 -1, i32 1>)
  ret float %r
}
define <2 x float> @array(<3 x i32> %coord, i32 %sample) #0 {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 8) @llvm.dx.resource.handlefromheap.tdx.MSTexture_v4f32_0_4_0_8t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.load.ms.v2f32.tdx.MSTexture_v4f32_0_4_0_8t.v3i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 8) %t, <3 x i32> %coord, i32 %sample, <2 x i32> <i32 1, i32 -1>)
  ret <2 x float> %r
}
define float @mixed_scalar() {
  %t = call target("dx.MSTexture", i32, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_4_0_3t(i32 0)
  %r = call float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_i32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", i32, 0, 4, 0, 3) %t, <2 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @bad_coord() {
  %t = call target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_v4f32_0_4_0_3t(i32 0)
  %r = call float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_v4f32_0_4_0_3t.i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) %t, i32 0, i32 0, <2 x i32> zeroinitializer)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.MSTexture", float, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_f32_0_4_0_3t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.load.ms.v4f32.tdx.MSTexture_f32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", float, 0, 4, 0, 3) %t, <2 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret <4 x float> %r
}
; CHECK: !feme.cpu.resources = !{![[P:[0-9]+]], ![[A:[0-9]+]]}
; CHECK: ![[P]] = !{!"plain", i32 0, i1 false
; CHECK: ![[A]] = !{!"array", i32 0, i1 false
declare target("dx.MSTexture", <4 x float>, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_v4f32_0_4_0_3t(i32)
declare target("dx.MSTexture", <4 x float>, 0, 4, 0, 8) @llvm.dx.resource.handlefromheap.tdx.MSTexture_v4f32_0_4_0_8t(i32)
declare target("dx.MSTexture", i32, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_4_0_3t(i32)
declare target("dx.MSTexture", float, 0, 4, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_f32_0_4_0_3t(i32)
declare float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_v4f32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3), <2 x i32>, i32, <2 x i32>)
declare <2 x float> @llvm.dx.resource.load.ms.v2f32.tdx.MSTexture_v4f32_0_4_0_8t.v3i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 8), <3 x i32>, i32, <2 x i32>)
declare float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_i32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", i32, 0, 4, 0, 3), <2 x i32>, i32, <2 x i32>)
declare float @llvm.dx.resource.load.ms.f32.tdx.MSTexture_v4f32_0_4_0_3t.i32.v2i32(target("dx.MSTexture", <4 x float>, 0, 4, 0, 3), i32, i32, <2 x i32>)
declare <4 x float> @llvm.dx.resource.load.ms.v4f32.tdx.MSTexture_f32_0_4_0_3t.v2i32.v2i32(target("dx.MSTexture", float, 0, 4, 0, 3), <2 x i32>, i32, <2 x i32>)
attributes #0 = { "feme.shader.stage"="fragment" }
