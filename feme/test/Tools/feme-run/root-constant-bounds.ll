; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=FULL
; RUN: feme-run --wave-size=8 --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=FULL
; RUN: feme-run --reference --repeat=3 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=FULL
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --target=%feme_host_triple --wave-size=4 %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=3 --heap=%t/heap.yaml %t/shader.o | FileCheck %s --check-prefix=FULL
; RUN: feme-run --object --heap=%t/short.yaml %t/shader.o | FileCheck %s --check-prefix=SHORT
; RUN: feme-run --object --heap=%t/empty.yaml %t/shader.o | FileCheck %s --check-prefix=EMPTY
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Six lanes: two partial rows, a cross-element read, array arithmetic wrapping
; in i32, row arithmetic wrapping in i32, and UINT32_MAX array/row indices.
; FULL-COUNT-3: heap[0]: 1 0 2 0 4 0 5 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 123456789 987654321{{$}}
; FULL-NOT: heap[
; SHORT: heap[0]: 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 123456789 987654321{{$}}
; EMPTY: heap[0]: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 123456789 987654321{{$}}

;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %array = icmp eq i32 %tid, 1
  %wrapindex = icmp eq i32 %tid, 3
  %last = icmp eq i32 %tid, 5
  %baseindex = select i1 %array, i32 1, i32 0
  %largeindex = select i1 %wrapindex, i32 1073741824, i32 %baseindex
  %index = select i1 %last, i32 -1, i32 %largeindex
  %cross = icmp eq i32 %tid, 2
  %wraprow = icmp eq i32 %tid, 4
  %baserow = select i1 %cross, i32 1, i32 0
  %largerow = select i1 %wraprow, i32 268435456, i32 %baserow
  %row = select i1 %last, i32 -1, i32 %largerow
  %cb = call target("dx.CBuffer", [12 x i8]) @llvm.dx.resource.handlefrombinding(i32 7, i32 3, i32 2, i32 %index, ptr null)
  %r = call {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [12 x i8]) %cb, i32 %row)
  %r64 = call {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [12 x i8]) %cb, i32 %row)
  %first = extractvalue {i32,i32,i32,i32} %r, 0
  %tail = extractvalue {i32,i32,i32,i32} %r, 3
  %wide = extractvalue {i64,i64} %r64, 0
  %widehigh = lshr i64 %wide, 32
  %high = trunc i64 %widehigh to i32
  %widetail = extractvalue {i64,i64} %r64, 1
  %clipped = trunc i64 %widetail to i32
  %offset = mul i32 %tid, 16
  %offset1 = add i32 %offset, 4
  %offset2 = add i32 %offset, 8
  %offset3 = add i32 %offset, 12
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32 0)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset, i32 poison, i32 %first)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset1, i32 poison, i32 %tail)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset2, i32 poison, i32 %high)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %offset3, i32 poison, i32 %clipped)
  ret void
}
declare target("dx.CBuffer", [12 x i8]) @llvm.dx.resource.handlefrombinding(i32,i32,i32,i32,ptr)
declare {i32,i32,i32,i32} @llvm.dx.resource.load.cbufferrow.4.i32(target("dx.CBuffer", [12 x i8]),i32)
declare {i64,i64} @llvm.dx.resource.load.cbufferrow.2.i64(target("dx.CBuffer", [12 x i8]),i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap(i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0),i32,i32,i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="6,1,1" }

;--- heap.yaml
root-constants: [1, 2, 3, 4, 5, 6]
resource-heap:
  - index: 0
    size: 104
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
;--- short.yaml
root-constants: [1]
resource-heap:
  - index: 0
    size: 104
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
;--- empty.yaml
resource-heap:
  - index: 0
    size: 104
    data: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789, 987654321]
