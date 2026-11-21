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
; Three-byte elements require align 1, not the root block's align 4.
; The extra supplied bytes must not complete a component beyond its element.
; CHECK-COUNT-3: heap[0]: 513 0 1284 0 0 0 123456789 987654321{{$}}
; CHECK-NOT: heap[

;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cb = call target("dx.CBuffer", [3 x i8]) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 2, i32 %tid, ptr null)
  %r = call {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [3 x i8]) %cb, i32 0)
  %first16 = extractvalue {i16,i16,i16,i16,i16,i16,i16,i16} %r, 0
  %clipped16 = extractvalue {i16,i16,i16,i16,i16,i16,i16,i16} %r, 1
  %first = zext i16 %first16 to i32
  %clipped = zext i16 %clipped16 to i32
  %offset = mul i32 %tid, 8
  %offset1 = add i32 %offset, 4
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset, i32 poison, i32 %first)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset1, i32 poison, i32 %clipped)
  ret void
}
declare target("dx.CBuffer", [3 x i8]) @llvm.dx.resource.handlefrombinding(i32,i32,i32,i32,ptr)
declare {i16,i16,i16,i16,i16,i16,i16,i16} @llvm.dx.resource.load.cbufferrow.8.i16(target("dx.CBuffer", [3 x i8]),i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0),i32,i32,i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="3,1,1" }

;--- heap.yaml
root-constants: [67305985, 134678021]
resource-heap:
  - index: 0
    size: 32
    data: [0, 0, 0, 0, 0, 0, 123456789, 987654321]
