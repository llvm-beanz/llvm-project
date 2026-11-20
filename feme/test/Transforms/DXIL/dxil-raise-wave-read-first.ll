; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | opt -S -passes=verify | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define i16 @first_i16(
; CHECK: call i16 @llvm.dx.wave.readlane.first.i16(i16 %v)
define i16 @first_i16(i16 %v) {
  %r = call i16 @dx.op.waveReadLaneFirst.i16(i32 118, i16 %v)
  ret i16 %r
}

; CHECK-LABEL: define i32 @first_i32(
; CHECK: call i32 @llvm.dx.wave.readlane.first.i32(i32 %v)
define i32 @first_i32(i32 %v) {
  %r = call i32 @dx.op.waveReadLaneFirst.i32(i32 118, i32 %v)
  ret i32 %r
}

; CHECK-LABEL: define i64 @first_i64(
; CHECK: call i64 @llvm.dx.wave.readlane.first.i64(i64 %v)
define i64 @first_i64(i64 %v) {
  %r = call i64 @dx.op.waveReadLaneFirst.i64(i32 118, i64 %v)
  ret i64 %r
}

; CHECK-LABEL: define half @first_f16(
; CHECK: call half @llvm.dx.wave.readlane.first.f16(half %v)
define half @first_f16(half %v) {
  %r = call half @dx.op.waveReadLaneFirst.f16(i32 118, half %v)
  ret half %r
}

; CHECK-LABEL: define float @first_f32(
; CHECK: call float @llvm.dx.wave.readlane.first.f32(float %v)
define float @first_f32(float %v) {
  %r = call float @dx.op.waveReadLaneFirst.f32(i32 118, float %v)
  ret float %r
}

; CHECK-LABEL: define double @first_f64(
; CHECK: call double @llvm.dx.wave.readlane.first.f64(double %v)
define double @first_f64(double %v) {
  %r = call double @dx.op.waveReadLaneFirst.f64(i32 118, double %v)
  ret double %r
}

declare i16 @dx.op.waveReadLaneFirst.i16(i32, i16)
declare i32 @dx.op.waveReadLaneFirst.i32(i32, i32)
declare i64 @dx.op.waveReadLaneFirst.i64(i32, i64)
declare half @dx.op.waveReadLaneFirst.f16(i32, half)
declare float @dx.op.waveReadLaneFirst.f32(i32, float)
declare double @dx.op.waveReadLaneFirst.f64(i32, double)
