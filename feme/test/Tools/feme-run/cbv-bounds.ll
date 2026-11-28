; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --wave-size=8 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --target=%feme_host_triple --wave-size=4 %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=3 --heap=%t/object.yaml %t/shader.o | FileCheck %s --check-prefix=AOT
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Two CBVs, dynamic array/row indices, partial declared rows, short host
; descriptors, and 16/32/64-bit integer/floating-point loads.
; CHECK-COUNT-3: heap[0]: 6 0 16376 2 1073217536 15 2 10 0 16376 2 1073217536 15 2 0 0 16376 2 1073217536 15 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 123456789 987654321{{$}}
; CHECK-NOT: heap[
; AOT-COUNT-3: heap[3]: 6 0 16376 2 1073217536 15 2 10 0 16376 2 1073217536 15 2 0 0 16376 2 1073217536 15 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 123456789 987654321{{$}}

;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %idx = urem i32 %tid, 3
  %row = udiv i32 %tid, 3
  %a = call target("dx.CBuffer", [8 x i8]) @llvm.dx.resource.handlefrombinding.a(i32 7, i32 0, i32 2, i32 %idx, ptr null)
  %b = call target("dx.CBuffer", [32 x i8]) @llvm.dx.resource.handlefrombinding.b(i32 7, i32 2, i32 1, i32 0, ptr null)
  %ar = call {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [8 x i8]) %a, i32 %row)
  %r16 = call {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [32 x i8]) %b, i32 %row)
  %rh = call {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16(target("dx.CBuffer", [32 x i8]) %b, i32 %row)
  %r64 = call {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [32 x i8]) %b, i32 %row)
  %rd = call {double,double} @llvm.dx.resource.load.cbufferrow.2.f64(target("dx.CBuffer", [32 x i8]) %b, i32 %row)
  %rf = call {float,float,float,float} @llvm.dx.resource.load.cbufferrow.4.f32(target("dx.CBuffer", [32 x i8]) %b, i32 %row)
  %v0 = extractvalue {i32,i32,i32,i32} %ar, 0
  %v1 = extractvalue {i32,i32,i32,i32} %ar, 2
  %n16 = extractvalue {i16,i16,i16,i16,i16,i16,i16,i16} %r16, 3
  %v2 = zext i16 %n16 to i32
  %h = extractvalue {half,half,half,half,half,half,half,half} %rh, 5
  %hf = fpext half %h to float
  %v3 = fptoui float %hf to i32
  %n64 = extractvalue {i64,i64} %r64, 0
  %high = lshr i64 %n64, 32
  %v4 = trunc i64 %high to i32
  %d = extractvalue {double,double} %rd, 0
  %times10 = fmul double %d, 1.000000e+01
  %v5 = fptoui double %times10 to i32
  %f = extractvalue {float,float,float,float} %rf, 2
  %v6 = fptoui float %f to i32
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  %base = mul i32 %tid, 28
  %off1 = add i32 %base, 4
  %off2 = add i32 %base, 8
  %off3 = add i32 %base, 12
  %off4 = add i32 %base, 16
  %off5 = add i32 %base, 20
  %off6 = add i32 %base, 24
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %v0)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off1, i32 poison, i32 %v1)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off2, i32 poison, i32 %v2)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off3, i32 poison, i32 %v3)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off4, i32 poison, i32 %v4)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off5, i32 poison, i32 %v5)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off6, i32 poison, i32 %v6)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare target("dx.CBuffer", [8 x i8]) @llvm.dx.resource.handlefrombinding.a(i32,i32,i32,i32,ptr)
declare target("dx.CBuffer", [32 x i8]) @llvm.dx.resource.handlefrombinding.b(i32,i32,i32,i32,ptr)
declare {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [8 x i8]),i32)
declare {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [32 x i8]),i32)
declare {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16(target("dx.CBuffer", [32 x i8]),i32)
declare {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [32 x i8]),i32)
declare {double,double} @llvm.dx.resource.load.cbufferrow.2.f64(target("dx.CBuffer", [32 x i8]),i32)
declare {float,float,float,float} @llvm.dx.resource.load.cbufferrow.4.f32(target("dx.CBuffer", [32 x i8]),i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0),i32,i32,i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }

;--- heap.yaml
bindings:
  - space: 7
    register: 0
    register-class: cbv
    entries:
      - index: 0
        size: 16
        data: [6, 7, 777, 888]
      - index: 1
        size: 16
        data: [10, 11, 999, 222]
  - space: 7
    register: 2
    register-class: cbv
    entries:
      - index: 0
        size: 16
        data: [0, 1073217536, 1073741824, 0]
resource-heap:
  - index: 0
    size: 148
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
;--- object.yaml
resource-heap:
  - index: 0
    size: 16
    data: [6, 7, 777, 888]
  - index: 1
    size: 16
    data: [10, 11, 999, 222]
  - index: 2
    size: 16
    data: [0, 1073217536, 1073741824, 0]
  - index: 3
    size: 148
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
