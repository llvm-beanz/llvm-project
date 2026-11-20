; RUN: feme-opt --llvm -passes=feme-cpu-lower-wave -S %s | opt -S -passes=instcombine,verify | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; CHECK-LABEL: define i32 @partial(
; CHECK: ret i32 42
define i32 @partial() {
  %r = call i32 @feme.cpu.wave.readfirst.i32.v4(
    <4 x i1> <i1 false, i1 false, i1 true, i1 true>,
    <4 x i32> <i32 poison, i32 poison, i32 42, i32 99>)
  ret i32 %r
}

; CHECK-LABEL: define i32 @empty(
; CHECK: ret i32 0
define i32 @empty() {
  %r = call i32 @feme.cpu.wave.readfirst.i32.v4(
    <4 x i1> zeroinitializer, <4 x i32> poison)
  ret i32 %r
}

; CHECK-LABEL: define i32 @float_bits(
; CHECK: ret i32 -2147483648
define i32 @float_bits() {
  %r = call float @feme.cpu.wave.readfirst.f32.v4(
    <4 x i1> <i1 false, i1 true, i1 true, i1 false>,
    <4 x float> <float poison, float -0.000000e+00, float 1.000000e+00, float poison>)
  %bits = bitcast float %r to i32
  ret i32 %bits
}

; CHECK-LABEL: define i16 @short(
; CHECK: ret i16 -1
define i16 @short() {
  %r = call i16 @feme.cpu.wave.readfirst.i16.v4(
    <4 x i1> <i1 false, i1 false, i1 false, i1 true>,
    <4 x i16> <i16 poison, i16 poison, i16 poison, i16 -1>)
  ret i16 %r
}

; CHECK-LABEL: define i64 @long(
; CHECK: ret i64 4294967296
define i64 @long() {
  %r = call i64 @feme.cpu.wave.readfirst.i64.v4(
    <4 x i1> <i1 true, i1 false, i1 false, i1 false>,
    <4 x i64> <i64 4294967296, i64 poison, i64 poison, i64 poison>)
  ret i64 %r
}

declare i32 @feme.cpu.wave.readfirst.i32.v4(<4 x i1>, <4 x i32>)

; CHECK-LABEL: define i16 @half_bits(
; CHECK: ret i16 -32768
define i16 @half_bits() {
  %r = call half @feme.cpu.wave.readfirst.f16.v4(
    <4 x i1> <i1 false, i1 true, i1 false, i1 false>,
    <4 x half> <half poison, half 0xH8000, half poison, half poison>)
  %bits = bitcast half %r to i16
  ret i16 %bits
}

; CHECK-LABEL: define i64 @double_bits(
; CHECK: ret i64 -9223372036854775808
define i64 @double_bits() {
  %r = call double @feme.cpu.wave.readfirst.f64.v4(
    <4 x i1> <i1 false, i1 true, i1 false, i1 false>,
    <4 x double> <double poison, double -0.000000e+00, double poison, double poison>)
  %bits = bitcast double %r to i64
  ret i64 %bits
}

; CHECK-LABEL: define i1 @boolean(
; CHECK: ret i1 true
define i1 @boolean() {
  %r = call i1 @feme.cpu.wave.readfirst.i1.v4(
    <4 x i1> <i1 false, i1 false, i1 true, i1 false>,
    <4 x i1> <i1 poison, i1 poison, i1 true, i1 poison>)
  ret i1 %r
}

declare half @feme.cpu.wave.readfirst.f16.v4(<4 x i1>, <4 x half>)
declare double @feme.cpu.wave.readfirst.f64.v4(<4 x i1>, <4 x double>)
declare i1 @feme.cpu.wave.readfirst.i1.v4(<4 x i1>, <4 x i1>)
declare float @feme.cpu.wave.readfirst.f32.v4(<4 x i1>, <4 x float>)
declare i16 @feme.cpu.wave.readfirst.i16.v4(<4 x i1>, <4 x i16>)
declare i64 @feme.cpu.wave.readfirst.i64.v4(<4 x i1>, <4 x i64>)
