; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops,verify -S %s | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Preserve legacy sampler modes and range-relative indices. Unknown consumers
; retain the existing handle cast bridge, not an invalid replacement type.

%dx.types.Handle = type { ptr }

; CHECK-LABEL: define %dx.types.Handle @default_sampler(
define %dx.types.Handle @default_sampler(i32 %absolute) {
  ; CHECK: [[INDEX:%.*]] = sub i32 %absolute, 3
  ; CHECK: [[H:%.*]] = call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 7, i32 3, i32 2, i32 [[INDEX]], ptr null)
  ; CHECK: [[CAST:%.*]] = call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Sampler", 0) [[H]])
  ; CHECK: ret %dx.types.Handle [[CAST]]
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 0, i32 %absolute, i1 true)
  ret %dx.types.Handle %h
}

; CHECK-LABEL: define %dx.types.Handle @comparison_sampler(
define %dx.types.Handle @comparison_sampler() {
  ; CHECK: [[H:%.*]] = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 9, i32 5, i32 1, i32 0, ptr null)
  ; CHECK: [[CAST:%.*]] = call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Sampler", 1) [[H]])
  ; CHECK: ret %dx.types.Handle [[CAST]]
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 1, i32 5, i1 false)
  ret %dx.types.Handle %h
}

; CHECK-LABEL: define %dx.types.Handle @unbounded_sampler(
define %dx.types.Handle @unbounded_sampler(i32 %index) {
  ; CHECK: call target("dx.Sampler", 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 11, i32 0, i32 0, i32 %index, ptr null)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 2, i32 %index, i1 false)
  ret %dx.types.Handle %h
}

; CHECK-LABEL: define %dx.types.Handle @mono_sampler(
define %dx.types.Handle @mono_sampler() {
  ; CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 3, i32 0, i1 false)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 3, i32 0, i1 false)
  ret %dx.types.Handle %h
}

; CHECK-LABEL: define %dx.types.Handle @unknown_sampler(
define %dx.types.Handle @unknown_sampler() {
  ; CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 4, i32 0, i1 false)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 4, i32 0, i1 false)
  ret %dx.types.Handle %h
}

; CHECK-LABEL: define %dx.types.Handle @malformed_sampler(
define %dx.types.Handle @malformed_sampler() {
  ; CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 5, i32 0, i1 false)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 5, i32 0, i1 false)
  ret %dx.types.Handle %h
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
!dx.resources = !{!0}
!0 = !{null, null, null, !1}
!1 = !{!2, !3, !4, !5, !6, !7}
; Samplers: {ID, GV, name, space, lowerBound, rangeSize, samplerMode, props}.
!2 = !{i32 0, ptr undef, !"default", i32 7, i32 3, i32 2, i32 0, null}
!3 = !{i32 1, ptr undef, !"comparison", i32 9, i32 5, i32 1, i32 1, null}
!4 = !{i32 2, ptr undef, !"unbounded", i32 11, i32 0, i32 -1, i32 0, null}
!5 = !{i32 3, ptr undef, !"mono", i32 0, i32 0, i32 1, i32 2, null}
!6 = !{i32 4, ptr undef, !"unknown", i32 0, i32 0, i32 1, i32 3, null}
!7 = !{i32 5, ptr undef, !"malformed", i32 0, i32 0, i32 1, !"comparison", null}
