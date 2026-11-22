; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Compare result field zero is scalar. Unsupported uses must not add another
; texture read before leaving the original operation unraised.

%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }

; CHECK-LABEL: define float @comparison(
define float @comparison(float %u, float %v, float %reference) {
  ; CHECK: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK-SAME: float %reference, <2 x i32> <i32 1, i32 -2>)
  ; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float %u, float %v, float poison, float poison, i32 1, i32 -2, i32 poison, float %reference)
  %value = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %value
}

; CHECK-LABEL: define i32 @status(
define i32 @status() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret i32
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5)
  %value = extractvalue %dx.types.ResRet.f32 %r, 4
  ret i32 %value
}

; CHECK-LABEL: define %dx.types.ResRet.f32 @aggregate(
define %dx.types.ResRet.f32 @aggregate() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret %dx.types.ResRet.f32
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5)
  ret %dx.types.ResRet.f32 %r
}

; CHECK-LABEL: define float @default_sampler(
define float @default_sampler() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret float
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 14, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5)
  %value = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %value
}

; CHECK-LABEL: define float @component_one(
define float @component_one() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret float
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5)
  %value = extractvalue %dx.types.ResRet.f32 %r, 1
  ret float %value
}

; CHECK-LABEL: define float @texture_3d(
define float @texture_3d() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret float
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 4, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float 0.0, float poison, i32 0, i32 0, i32 0, float 0.5)
  %value = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %value
}

; CHECK-LABEL: define float @invalid_reference(
define float @invalid_reference() {
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.badref
  ; CHECK-NOT: call float @llvm.dx.resource.samplecmplevelzero
  ; CHECK: ret float
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.badref(i32 65, %dx.types.Handle %t, %dx.types.Handle %s, float 0.0, float 0.0, float poison, float poison, i32 0, i32 0, i32 poison, double 0.5)
  %value = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %value
}

declare %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.badref(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, double)
declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float)
