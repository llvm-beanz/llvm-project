; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }

; CHECK-LABEL: define float @unclamped(
; CHECK: call float @llvm.dx.resource.samplecmp.f32
; CHECK-SAME: float %reference, <2 x i32> <i32 1, i32 -2>)
; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.sampleCmp
define float @unclamped(float %reference) {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 1, i32 -2, i32 poison, float %reference, float poison)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @zero_clamp(
; CHECK: call float @llvm.dx.resource.samplecmp.clamp.f32
; CHECK-SAME: float 5.000000e-01, <2 x i32> zeroinitializer, float 0.000000e+00)
; CHECK-NOT: call %dx.types.ResRet.f32 @dx.op.sampleCmp
define float @zero_clamp() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, float 0.0)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define i32 @status(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret i32
define i32 @status() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 4
  ret i32 %v
}
; CHECK-LABEL: define %dx.types.ResRet.f32 @aggregate(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret %dx.types.ResRet.f32
define %dx.types.ResRet.f32 @aggregate() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, float undef)
  ret %dx.types.ResRet.f32 %r
}
; CHECK-LABEL: define float @extra_component(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret float
define float @extra_component() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 1
  ret float %v
}
; CHECK-LABEL: define float @default_sampler(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret float
define float @default_sampler() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 14, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @bad_clamp(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.bad
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret float
define float @bad_clamp() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 2, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.bad(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float poison, float poison, i32 0, i32 0, i32 poison, float 0.5, i32 0)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
; CHECK-LABEL: define float @texture_3d(
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32
; CHECK-NOT: call float @llvm.dx.resource.samplecmp
; CHECK: ret float
define float @texture_3d() {
  %t0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 false, i1 false)
  %t = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %t0, %dx.types.ResourceProperties {i32 4, i32 1033})
  %s0 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 0, i1 true, i1 false)
  %s = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %s0, %dx.types.ResourceProperties {i32 32782, i32 0})
  %r = call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, %dx.types.Handle %t, %dx.types.Handle %s, float 0.25, float 0.75, float 0.5, float poison, i32 0, i32 0, i32 0, float 0.5, float undef)
  %v = extractvalue %dx.types.ResRet.f32 %r, 0
  ret float %v
}
declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float)
declare %dx.types.ResRet.f32 @dx.op.sampleCmp.bad(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, i32)
