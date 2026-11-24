; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }

; CHECK-LABEL: define float @plain(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-SAME: i32 %x, i32 %mip, i32 0)
; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.textureLoad
define float @plain(i32 %x, i32 %mip) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 1, i32 265 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %mip, i32 %x, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @array(
; CHECK: insertelement <2 x i32> poison, i32 %x, i32 0
; CHECK: insertelement <2 x i32> {{.*}}, i32 %layer, i32 1
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-SAME: i32 %mip, i32 0)
define float @array(i32 %x, i32 %layer, i32 %mip) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 6, i32 265 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %mip, i32 %x, i32 %layer, i32 poison, i32 poison, i32 poison, i32 poison)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32)
