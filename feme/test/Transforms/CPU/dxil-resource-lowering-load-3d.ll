; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s --implicit-check-not=@feme.stage.derivative
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define float @missing_z(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @short_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @wide_mip(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @dynamic_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @partial_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define <4 x float> @too_wide(
; CHECK: call <4 x float> @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @non_heap(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define i64 @wide_integer_texel(
; CHECK: call i64 @llvm.dx.resource.load.level
; CHECK-NOT: @feme.cpu.image.load
; CHECK-LABEL: define float @scalar(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <3 x i32> %coord, i64 0
; CHECK: [[OX:%.*]] = add i32 [[X]], 1
; CHECK: [[Y:%.*]] = extractelement <3 x i32> %coord, i64 1
; CHECK: [[OY:%.*]] = add i32 [[Y]], -1
; CHECK: [[Z:%.*]] = extractelement <3 x i32> %coord, i64 2
; CHECK: [[OZ:%.*]] = add i32 [[Z]], 2
; CHECK: [[P:%.*]] = call <4 x float> @feme.cpu.image.load.3d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 %image, i32 [[OX]], i32 [[OY]], i32 [[OZ]], i32 %mip, i32 0, i1 true)
; CHECK: extractelement <4 x float> [[P]], i64 0
; CHECK-NOT: call target("dx.
; CHECK-LABEL: define <2 x float> @vector(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: add i32 {{.*}}, -1
; CHECK: add i32 {{.*}}, 2
; CHECK: add i32 {{.*}}, -3
; CHECK: [[V:%.*]] = call <4 x float> @feme.cpu.image.load.3d.v4f32(
; CHECK-SAME: i32 %mip, i32 0, i1 true)
; CHECK: shufflevector <4 x float> [[V]], <4 x float> poison, <2 x i32> <i32 0, i32 1>
; CHECK-NOT: call target("dx.
define float @scalar(i32 %image, <3 x i32> %coord, i32 %mip) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 %image)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> %coord, i32 %mip, <3 x i32> <i32 1, i32 -1, i32 2>)
  ret float %r
}
define <2 x float> @vector(<3 x i32> %coord, i32 %mip) #0 {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call <2 x float> @llvm.dx.resource.load.level.v2f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> %coord, i32 %mip, <3 x i32> <i32 -1, i32 2, i32 -3>)
  ret <2 x float> %r
}
define float @missing_z() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v2i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <2 x i32> zeroinitializer, i32 0, <3 x i32> zeroinitializer)
  ret float %r
}
define float @short_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <2 x i32> zeroinitializer)
  ret float %r
}
define float @wide_mip() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i64.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i64 0, <3 x i32> zeroinitializer)
  ret float %r
}
define float @dynamic_offset(<3 x i32> %offset) {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <3 x i32> %offset)
  ret float %r
}
define float @partial_offset() {
  %t = call target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32 0)
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <3 x i32> <i32 0, i32 undef, i32 0>)
  ret float %r
}
define <4 x float> @too_wide() {
  %t = call target("dx.Texture", float, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_4t(i32 0)
  %r = call <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", float, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <3 x i32> zeroinitializer)
  ret <4 x float> %r
}
define float @non_heap(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t) {
  %r = call float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <3 x i32> zeroinitializer)
  ret float %r
}
define i64 @wide_integer_texel() {
  %t = call target("dx.Texture", i64, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_i64_0_0_0_4t(i32 0)
  %r = call i64 @llvm.dx.resource.load.level.i64.tdx.Texture_i64_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", i64, 0, 0, 0, 4) %t, <3 x i32> zeroinitializer, i32 0, <3 x i32> zeroinitializer)
  ret i64 %r
}
; CHECK: !feme.cpu.resources = !{![[SCALAR:[0-9]+]], ![[VECTOR:[0-9]+]]}
; CHECK: ![[SCALAR]] = !{!"scalar", i32 0, i1 false
; CHECK: ![[VECTOR]] = !{!"vector", i32 0, i1 false
declare target("dx.Texture", <4 x float>, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_v4f32_0_0_0_4t(i32)
declare target("dx.Texture", float, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_f32_0_0_0_4t(i32)
declare target("dx.Texture", i64, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_i64_0_0_0_4t(i32)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), <3 x i32>, i32, <3 x i32>)
declare <2 x float> @llvm.dx.resource.load.level.v2f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), <3 x i32>, i32, <3 x i32>)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v2i32.i32.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), <2 x i32>, i32, <3 x i32>)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i32.v2i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), <3 x i32>, i32, <2 x i32>)
declare float @llvm.dx.resource.load.level.f32.tdx.Texture_v4f32_0_0_0_4t.v3i32.i64.v3i32(target("dx.Texture", <4 x float>, 0, 0, 0, 4), <3 x i32>, i64, <3 x i32>)
declare <4 x float> @llvm.dx.resource.load.level.v4f32.tdx.Texture_f32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", float, 0, 0, 0, 4), <3 x i32>, i32, <3 x i32>)
declare i64 @llvm.dx.resource.load.level.i64.tdx.Texture_i64_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", i64, 0, 0, 0, 4), <3 x i32>, i32, <3 x i32>)
attributes #0 = { "feme.shader.stage"="vertex" }
