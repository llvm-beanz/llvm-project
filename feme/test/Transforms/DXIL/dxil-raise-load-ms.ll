; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,feme-dxil-raise-ops,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }

; CHECK-LABEL: define float @plain(
; CHECK: [[T:%.*]] = call target("dx.MSTexture", float, 0, 4, 1, 3) @llvm.dx.resource.handlefromheap
; CHECK: [[X:%.*]] = insertelement <2 x i32> poison, i32 %x, i32 0
; CHECK: [[XY:%.*]] = insertelement <2 x i32> [[X]], i32 %y, i32 1
; CHECK: call float @llvm.dx.resource.load.ms{{.*}}(target("dx.MSTexture", float, 0, 4, 1, 3) [[T]], <2 x i32> [[XY]], i32 %sample, <2 x i32> zeroinitializer)
; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.textureLoad
define float @plain(i32 %x, i32 %y, i32 %sample) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 true)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 3, i32 262409 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %sample, i32 %x, i32 %y, i32 undef, i32 undef, i32 undef, i32 undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @array(
; CHECK: insertelement <3 x i32> {{.*}}, i32 %layer, i32 2
; CHECK: call <4 x float> @llvm.dx.resource.load.ms
; CHECK-SAME: i32 %sample, <2 x i32> <i32 1, i32 -1>)
; CHECK: extractelement <4 x float> {{.*}}, i64 3
define float @array(i32 %x, i32 %y, i32 %layer, i32 %sample) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 8, i32 263177 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %sample, i32 %x, i32 %y, i32 %layer, i32 1, i32 -1, i32 undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 3
  ret float %v
}
; CHECK-LABEL: define i32 @integer(
; CHECK: call <4 x i32> @llvm.dx.resource.load.ms
; CHECK-SAME: i32 %sample, <2 x i32> zeroinitializer)
define i32 @integer(i32 %sample) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 3, i32 263173 })
  %r = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %t, i32 %sample, i32 0, i32 0, i32 undef, i32 poison, i32 poison, i32 undef)
  %v = extractvalue %dx.types.ResRet.i32 %r, 2
  ret i32 %v
}
; CHECK-LABEL: define float @partial(
; CHECK: call float @llvm.dx.resource.load.ms
; CHECK-SAME: <2 x i32> <i32 0, i32 undef>)
define float @partial() {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 3, i32 262409 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 0, i32 0, i32 0, i32 undef, i32 0, i32 undef, i32 undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define i32 @status(
; CHECK-NOT: @llvm.dx.resource.load.ms
; CHECK: call %dx.types.ResRet.f32 @dx.op.textureLoad.f32
define i32 @status() {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 3, i32 262409 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 0, i32 0, i32 0, i32 undef, i32 undef, i32 undef, i32 undef)
  %s = extractvalue %dx.types.ResRet.f32 %r, 4
  ret i32 %s
}
declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32)
declare %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32)
