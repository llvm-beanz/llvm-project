; RUN: split-file %s %t
; RUN: feme-run --root-signature-constants --wave-size=4 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --root-signature-constants --wave-size=8 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --root-signature-constants --reference --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --root-signature-constants --repeat=3 --heap=%t/full.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --repeat=3 --heap=%t/full.yaml %t/shader.ll | FileCheck %s --check-prefix=INFERRED
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --root-signature-constants --target=%feme_host_triple --wave-size=4 %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=3 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; RUN: feme-run --object --repeat=3 --heap=%t/short.yaml %t/shader.o | FileCheck %s --check-prefix=SHORT
; RUN: not feme-run --object --root-signature-constants %t/shader.o 2>&1 | FileCheck %s --check-prefix=OBJECT
; RUN: not feme --root-signature-constants --target=dxil %t/shader.bc 2>&1 | FileCheck %s --check-prefix=TARGET
; RUN: not feme-run --inspect-root-signature --root-signature-constants %t/shader.bc 2>&1 | FileCheck %s --check-prefix=INSPECT
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; The shader declares 16 bytes, but the 1.1 root parameter exposes 8.
; Extra supplied bytes must not expand that parameter; the default inferred
; mode deliberately retains its old behavior. Three threads also cover tails.
; CHECK-COUNT-3: heap[0]: 42 43 0 0 42 43 0 0 42 43 0 0 123456789 987654321{{$}}
; CHECK-NOT: heap[
; INFERRED-COUNT-3: heap[0]: 42 43 99 100 42 43 99 100 42 43 99 100 123456789 987654321{{$}}
; INFERRED-NOT: heap[
; SHORT-COUNT-3: heap[0]: 42 0 0 0 42 0 0 0 42 0 0 0 123456789 987654321{{$}}
; SHORT-NOT: heap[
; OBJECT: '--root-signature-constants' is a compile option
; TARGET: '--root-signature-constants' requires the FeMe CPU target
; INSPECT: '--inspect-root-signature' cannot be combined with dispatch options

;--- shader.ll
define void @main() #0 !feme.dxil.rootsignature !0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cb = call target("dx.CBuffer", [16 x i8]) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 1, i32 0, ptr null)
  %r = call {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [16 x i8]) %cb, i32 0)
  %a = extractvalue {i32,i32,i32,i32} %r, 0
  %b = extractvalue {i32,i32,i32,i32} %r, 1
  %c = extractvalue {i32,i32,i32,i32} %r, 2
  %d = extractvalue {i32,i32,i32,i32} %r, 3
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  %base = mul i32 %tid, 16
  %off1 = add i32 %base, 4
  %off2 = add i32 %base, 8
  %off3 = add i32 %base, 12
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %a)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off1, i32 poison, i32 %b)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off2, i32 poison, i32 %c)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %off3, i32 poison, i32 %d)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare target("dx.CBuffer", [16 x i8]) @llvm.dx.resource.handlefrombinding(i32,i32,i32,i32,ptr)
declare {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [16 x i8]),i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0),i32,i32,i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="3,1,1" }
!0 = !{[48 x i8] c"\02\00\00\00\01\00\00\00\18\00\00\00\00\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\24\00\00\00\03\00\00\00\07\00\00\00\02\00\00\00"}

;--- heap.yaml
root-constants: [42, 43]
resource-heap:
  - index: 0
    size: 56
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
;--- full.yaml
root-constants: [42, 43, 99, 100]
resource-heap:
  - index: 0
    size: 56
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
;--- short.yaml
root-constants: [42]
resource-heap:
  - index: 0
    size: 56
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
