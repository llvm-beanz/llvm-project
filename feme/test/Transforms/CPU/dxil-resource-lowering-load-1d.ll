; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_layer(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @vector_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @dynamic_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @poison_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define <4 x float> @too_wide(
; CHECK: call <4 x float> @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define i64 @non_32_bit_texel(
; CHECK: call i64 @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @non_heap(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @plain(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = add i32 %coord, -1
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.load.1d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 %image, i32 [[X]], i32 %mip, i32 0, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @array(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[AX:%.*]] = extractelement <2 x i32> %coord, i64 0
; CHECK: [[OX:%.*]] = add i32 [[AX]], 1
; CHECK: [[L:%.*]] = extractelement <2 x i32> %coord, i64 1
; CHECK: [[A:%.*]] = call <4 x float> @feme.cpu.image.load.1darray.v4f32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 [[OX]], i32 [[L]], i32 %mip, i32 0, i1 true)
; CHECK: shufflevector <4 x float> [[A]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @plain(i32 %image, i32 %coord, i32 %mip) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 %image)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 %coord, i32 %mip, i32 -1)
  ret float %r
}
define <2 x float> @array(<2 x i32> %coord, i32 %mip) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.load.level.v2f32.tdx.Texture_v4f32_0_0_0_6t.v2i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, <2 x i32> %coord, i32 %mip, i32 1)
  ret <2 x float> %r
}
define float @missing_layer() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_6t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6) %t, i32 0, i32 0, i32 0)
  ret float %r
}
define float @vector_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 0, i32 0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(i32 %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 0, i32 0, i32 %offset)
  ret float %r
}
define float @poison_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 0, i32 0, i32 poison)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", float, 0, 0, 0, 1) %t, i32 0, i32 0, i32 0)
  ret <4 x float> %r
}
define i64 @non_32_bit_texel() {
  %t = call target("dx.Texture", i64, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_i64_0_0_0_1t(i32 0)
  %r = call i64 @llvm.dx.resource.load.level.i64.tdx.Texture_i64_0_0_0_1t.i32.i32.i32(target("dx.Texture", i64, 0, 0, 0, 1) %t, i32 0, i32 0, i32 0)
  ret i64 %r
}
define float @non_heap(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t) {
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1) %t, i32 0, i32 0, i32 0)
  ret float %r
}
; CHECK: !feme.cpu.resources = !{![[PLAIN:[0-9]+]], ![[ARRAY:[0-9]+]]}
; CHECK: ![[PLAIN]] = !{!"plain", i32 0, i1 false
; CHECK: ![[ARRAY]] = !{!"array", i32 0, i1 false
declare target("dx.Texture", <4 x float>, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_1t(i32)
declare target("dx.Texture", <4 x float>, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_6t(i32)
declare target("dx.Texture", float, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_1t(i32)
declare target("dx.Texture", i64, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_i64_0_0_0_1t(i32)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), i32, i32, i32)
declare <2 x float> @llvm.dx.resource.load.level.v2f32.tdx.Texture_v4f32_0_0_0_6t.v2i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), <2 x i32>, i32, i32)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_6t.i32.i32.i32(target("dx.Texture", <4 x float>, 0, 0, 0, 6), i32, i32, i32)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_1t.i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 1), i32, i32, <2 x i32>)
declare <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_f32_0_0_0_1t.i32.i32.i32(target("dx.Texture", float, 0, 0, 0, 1), i32, i32, i32)
declare i64 @llvm.dx.resource.load.level.i64.tdx.Texture_i64_0_0_0_1t.i32.i32.i32(target("dx.Texture", i64, 0, 0, 0, 1), i32, i32, i32)
attributes #0 = { "feme.shader.stage"="vertex" }
