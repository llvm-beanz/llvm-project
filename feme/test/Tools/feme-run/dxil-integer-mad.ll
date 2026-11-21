; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; DXIL integer multiply-add must expand before widening or scalar execution.
; Both signed and unsigned operations wrap rather than assuming no overflow.

; CHECK: heap[0]: 4294967295 5 4294967289 4294967295 4294967289 1 123456789 987654321{{$}}

;--- shader.ll
define void @main() #0 {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32 0)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  %loaded = call {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison)
  %old = extractvalue {i32, i1} %loaded, 0
  %signed = call i32 @llvm.dx.imad.i32(i32 %old, i32 3, i32 -1)
  %unsigned = call i32 @llvm.dx.umad.i32(i32 %signed, i32 2, i32 1)
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %unsigned)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap(i32)
declare {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.imad.i32(i32, i32, i32)
declare i32 @llvm.dx.umad.i32(i32, i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="6,1,1" }

;--- heap.yaml
resource-heap:
  - index: 0
    size: 32
    data: [0, 1, 2147483647, 2147483648, 4294967295, 715827883, 123456789, 987654321]
