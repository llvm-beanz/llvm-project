; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }

; Both DXC's undef padding and LLVM's poison padding encode an omitted offset.
; CHECK-LABEL: define float @omitted(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-SAME: i32 %mip, <3 x i32> zeroinitializer)
; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.textureLoad
define float @omitted(i32 %mip) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 4, i32 265 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %mip, i32 0, i32 1, i32 2, i32 undef, i32 undef, i32 undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @poison_padding(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-SAME: i32 %mip, <3 x i32> zeroinitializer)
define float @poison_padding(i32 %mip) {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 4, i32 265 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 %mip, i32 0, i32 1, i32 2, i32 poison, i32 poison, i32 poison)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; Partially undefined offsets must not be silently treated as a zero vector.
; CHECK-LABEL: define float @partial_offset(
; CHECK: call float @llvm.dx.resource.load.level
; CHECK-SAME: <3 x i32> <i32 1, i32 undef, i32 -1>)
define float @partial_offset() {
  %h = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h, %dx.types.ResourceProperties { i32 4, i32 265 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %t, i32 0, i32 0, i32 1, i32 2, i32 1, i32 undef, i32 -1)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32)
