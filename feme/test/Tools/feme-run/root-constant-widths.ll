; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --wave-size=8 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --target=%feme_host_triple --wave-size=4 %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=3 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; All six row types read the same bits, without changing the root-block ABI.
; CHECK-COUNT-3: heap[0]: 16376 2 1073217536 2 0 1073217536 15 123456789 987654321{{$}}
; CHECK-NOT: heap[

;--- shader.ll
define void @main() #0 {
  %cb = call target("dx.CBuffer", [16 x i8]) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
  %r16 = call {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %rh = call {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %r32 = call {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %rf = call {float,float,float,float} @llvm.dx.resource.load.cbufferrow.4.f32(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %r64 = call {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %rd = call {double,double} @llvm.dx.resource.load.cbufferrow.2.f64(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %n16 = extractvalue {i16,i16,i16,i16,i16,i16,i16,i16} %r16, 3
  %v16 = zext i16 %n16 to i32
  %h = extractvalue {half,half,half,half,half,half,half,half} %rh, 5
  %hf = fpext half %h to float
  %vh = fptoui float %hf to i32
  %v32 = extractvalue {i32,i32,i32,i32} %r32, 1
  %f = extractvalue {float,float,float,float} %rf, 2
  %vf = fptoui float %f to i32
  %n64 = extractvalue {i64,i64} %r64, 0
  %lo = trunc i64 %n64 to i32
  %high = lshr i64 %n64, 32
  %hi = trunc i64 %high to i32
  %d = extractvalue {double,double} %rd, 0
  %times10 = fmul double %d, 1.000000e+01
  %vd = fptoui double %times10 to i32
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 0, i32 poison, i32 %v16)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 4, i32 poison, i32 %vh)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 8, i32 poison, i32 %v32)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 12, i32 poison, i32 %vf)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 16, i32 poison, i32 %lo)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 20, i32 poison, i32 %hi)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 24, i32 poison, i32 %vd)
  ret void
}
declare target("dx.CBuffer", [16 x i8]) @llvm.dx.resource.handlefrombinding(i32,i32,i32,i32,ptr)
declare {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [16 x i8]),i32)
declare {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16(target("dx.CBuffer", [16 x i8]),i32)
declare {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [16 x i8]),i32)
declare {float,float,float,float} @llvm.dx.resource.load.cbufferrow.4.f32(target("dx.CBuffer", [16 x i8]),i32)
declare {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [16 x i8]),i32)
declare {double,double} @llvm.dx.resource.load.cbufferrow.2.f64(target("dx.CBuffer", [16 x i8]),i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0),i32,i32,i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="1,1,1" }

;--- heap.yaml
root-constants: [0, 1073217536, 1073741824, 0]
resource-heap:
  - index: 0
    size: 36
    data: [0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
