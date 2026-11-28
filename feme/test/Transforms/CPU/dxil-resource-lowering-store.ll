; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-lower-resources,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define void @readonly(
; CHECK: call void @llvm.dx.resource.store.texture
; CHECK-NOT: @feme.cpu.image.store
; CHECK-LABEL: define void @rov(
; CHECK: call void @llvm.dx.resource.store.texture
; CHECK-NOT: @feme.cpu.image.store
; CHECK-LABEL: define void @non_heap(
; CHECK: call void @llvm.dx.resource.store.texture
; CHECK-NOT: @feme.cpu.image.store
; CHECK-LABEL: define void @wrong_coordinates(
; CHECK: call void @llvm.dx.resource.store.texture
; CHECK-NOT: @feme.cpu.image.store
; CHECK-LABEL: define void @wrong_texel(
; CHECK: call void @llvm.dx.resource.store.texture
; CHECK-NOT: @feme.cpu.image.store
; CHECK-LABEL: define void @array2d(
; CHECK-SAME: ptr %image_heap, i32 %image_heap_count)
; CHECK: [[X:%.*]] = extractelement <3 x i32> %coord, i64 0
; CHECK: [[Y:%.*]] = extractelement <3 x i32> %coord, i64 1
; CHECK: [[L:%.*]] = extractelement <3 x i32> %coord, i64 2
; CHECK: call void @feme.cpu.image.store.2darray.v4i32(ptr %image_heap, i32 %image_heap_count, i32 %index, i32 [[X]], i32 [[Y]], i32 [[L]], <4 x i32> %value, i1 true)
; CHECK-NOT: call target("dx.
define void @array2d(i32 %index, <3 x i32> %coord, <4 x i32> %value) {
  %t = call target("dx.Texture", <4 x i32>, 1, 0, 0, 7) @llvm.dx.resource.handlefromheap.tarray(i32 %index)
  call void @llvm.dx.resource.store.texture.tarray.v3i32.v4i32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7) %t, <3 x i32> %coord, <4 x i32> %value)
  ret void
}

; CHECK-LABEL: define void @plain2d(
; CHECK: [[X2:%.*]] = extractelement <2 x i32> %coord, i64 0
; CHECK: [[Y2:%.*]] = extractelement <2 x i32> %coord, i64 1
; CHECK: call void @feme.cpu.image.store.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 [[X2]], i32 [[Y2]], <4 x float> %value, i1 true)
define void @plain2d(<2 x i32> %coord, <4 x float> %value) {
  %t = call target("dx.Texture", <4 x float>, 1, 0, 0, 2) @llvm.dx.resource.handlefromheap.tplain(i32 0)
  call void @llvm.dx.resource.store.texture.tplain.v2i32.v4f32(target("dx.Texture", <4 x float>, 1, 0, 0, 2) %t, <2 x i32> %coord, <4 x float> %value)
  ret void
}

; CHECK-LABEL: define void @plain1d(
; CHECK: [[P:%.*]] = insertelement <4 x i32> zeroinitializer, i32 %value, i64 0
; CHECK: call void @feme.cpu.image.store.1d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %x, <4 x i32> [[P]], i1 true)
define void @plain1d(i32 %x, i32 %value) {
  %t = call target("dx.Texture", i32, 1, 0, 0, 1) @llvm.dx.resource.handlefromheap.t1d(i32 1)
  call void @llvm.dx.resource.store.texture.t1d.i32.i32(target("dx.Texture", i32, 1, 0, 0, 1) %t, i32 %x, i32 %value)
  ret void
}

; CHECK-LABEL: define void @array1d(
; CHECK: extractelement <2 x float> %value, i64 0
; CHECK: [[V:%.*]] = insertelement <4 x float> {{.*}}, float {{.*}}, i64 1
; CHECK: [[XA:%.*]] = extractelement <2 x i32> %coord, i64 0
; CHECK: [[LA:%.*]] = extractelement <2 x i32> %coord, i64 1
; CHECK: call void @feme.cpu.image.store.1darray.v4f32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 [[XA]], i32 [[LA]], <4 x float> [[V]], i1 true)
define void @array1d(<2 x i32> %coord, <2 x float> %value) {
  %t = call target("dx.Texture", <2 x float>, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.t1da(i32 2)
  call void @llvm.dx.resource.store.texture.t1da.v2i32.v2f32(target("dx.Texture", <2 x float>, 1, 0, 0, 6) %t, <2 x i32> %coord, <2 x float> %value)
  ret void
}

; CHECK-LABEL: define void @volume(
; CHECK: [[VP:%.*]] = insertelement <4 x i32> zeroinitializer, i32 %value, i64 0
; CHECK: [[VX:%.*]] = extractelement <3 x i32> %coord, i64 0
; CHECK: [[VY:%.*]] = extractelement <3 x i32> %coord, i64 1
; CHECK: [[VZ:%.*]] = extractelement <3 x i32> %coord, i64 2
; CHECK: call void @feme.cpu.image.store.3d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 3, i32 [[VX]], i32 [[VY]], i32 [[VZ]], <4 x i32> [[VP]], i1 true)
define void @volume(<3 x i32> %coord, i32 %value) {
  %t = call target("dx.Texture", i32, 1, 0, 1, 4) @llvm.dx.resource.handlefromheap.t3d(i32 3)
  call void @llvm.dx.resource.store.texture.t3d.v3i32.i32(target("dx.Texture", i32, 1, 0, 1, 4) %t, <3 x i32> %coord, i32 %value)
  ret void
}

define void @readonly() {
  %t = call target("dx.Texture", <4 x i32>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tro(i32 0)
  call void @llvm.dx.resource.store.texture.tro.v3i32.v4i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 7) %t, <3 x i32> zeroinitializer, <4 x i32> zeroinitializer)
  ret void
}

define void @rov() {
  %t = call target("dx.Texture", <4 x i32>, 1, 1, 0, 7) @llvm.dx.resource.handlefromheap.trov(i32 0)
  call void @llvm.dx.resource.store.texture.trov.v3i32.v4i32(target("dx.Texture", <4 x i32>, 1, 1, 0, 7) %t, <3 x i32> zeroinitializer, <4 x i32> zeroinitializer)
  ret void
}

define void @non_heap(target("dx.Texture", <4 x i32>, 1, 0, 0, 7) %t) {
  call void @llvm.dx.resource.store.texture.tarray.v3i32.v4i32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7) %t, <3 x i32> zeroinitializer, <4 x i32> zeroinitializer)
  ret void
}

define void @wrong_coordinates() {
  %t = call target("dx.Texture", <4 x i32>, 1, 0, 0, 7) @llvm.dx.resource.handlefromheap.tarray(i32 0)
  call void @llvm.dx.resource.store.texture.tarray.v2i32.v4i32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7) %t, <2 x i32> zeroinitializer, <4 x i32> zeroinitializer)
  ret void
}

define void @wrong_texel() {
  %t = call target("dx.Texture", <4 x i32>, 1, 0, 0, 7) @llvm.dx.resource.handlefromheap.tarray(i32 0)
  call void @llvm.dx.resource.store.texture.tarray.v3i32.v4f32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7) %t, <3 x i32> zeroinitializer, <4 x float> zeroinitializer)
  ret void
}

declare target("dx.Texture", <4 x i32>, 1, 0, 0, 7) @llvm.dx.resource.handlefromheap.tarray(i32)
declare target("dx.Texture", <4 x float>, 1, 0, 0, 2) @llvm.dx.resource.handlefromheap.tplain(i32)
declare target("dx.Texture", i32, 1, 0, 0, 1) @llvm.dx.resource.handlefromheap.t1d(i32)
declare target("dx.Texture", <2 x float>, 1, 0, 0, 6) @llvm.dx.resource.handlefromheap.t1da(i32)
declare target("dx.Texture", i32, 1, 0, 1, 4) @llvm.dx.resource.handlefromheap.t3d(i32)
declare target("dx.Texture", <4 x i32>, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tro(i32)
declare target("dx.Texture", <4 x i32>, 1, 1, 0, 7) @llvm.dx.resource.handlefromheap.trov(i32)
declare void @llvm.dx.resource.store.texture.tarray.v3i32.v4i32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7), <3 x i32>, <4 x i32>)
declare void @llvm.dx.resource.store.texture.tplain.v2i32.v4f32(target("dx.Texture", <4 x float>, 1, 0, 0, 2), <2 x i32>, <4 x float>)
declare void @llvm.dx.resource.store.texture.t1d.i32.i32(target("dx.Texture", i32, 1, 0, 0, 1), i32, i32)
declare void @llvm.dx.resource.store.texture.t1da.v2i32.v2f32(target("dx.Texture", <2 x float>, 1, 0, 0, 6), <2 x i32>, <2 x float>)
declare void @llvm.dx.resource.store.texture.t3d.v3i32.i32(target("dx.Texture", i32, 1, 0, 1, 4), <3 x i32>, i32)
declare void @llvm.dx.resource.store.texture.tro.v3i32.v4i32(target("dx.Texture", <4 x i32>, 0, 0, 0, 7), <3 x i32>, <4 x i32>)
declare void @llvm.dx.resource.store.texture.trov.v3i32.v4i32(target("dx.Texture", <4 x i32>, 1, 1, 0, 7), <3 x i32>, <4 x i32>)
declare void @llvm.dx.resource.store.texture.tarray.v2i32.v4i32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7), <2 x i32>, <4 x i32>)
declare void @llvm.dx.resource.store.texture.tarray.v3i32.v4f32(target("dx.Texture", <4 x i32>, 1, 0, 0, 7), <3 x i32>, <4 x float>)
