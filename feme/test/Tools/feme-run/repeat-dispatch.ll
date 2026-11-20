; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=ONCE
; RUN: feme-run --wave-size=4 --repeat=1 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=ONCE
; RUN: feme-run --wave-size=4 --repeat=3 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=REPEAT
; RUN: feme-run --reference --repeat=3 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s --check-prefix=REPEAT
; RUN: not feme-run --repeat=0 %t/missing.ll 2>&1 | FileCheck %s --check-prefix=ZERO
;
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
;
; Accumulation distinguishes real retained dispatches from repeatedly printing
; one result or resetting resources. Both groups have a partial four-lane wave.

; ONCE: binding[0:0][0]: 11 22 33 44 55 66 123456789 987654321{{$}}
; ONCE-NOT: binding[
; REPEAT: binding[0:0][0]: 11 22 33 44 55 66 123456789 987654321{{$}}
; REPEAT-NEXT: binding[0:0][0]: 12 24 36 48 60 72 123456789 987654321{{$}}
; REPEAT-NEXT: binding[0:0][0]: 13 26 39 52 65 78 123456789 987654321{{$}}
; REPEAT-NOT: binding[
; AOT: heap[0]: 11 22 33 44 55 66 123456789 987654321{{$}}
; AOT-NEXT: heap[0]: 12 24 36 48 60 72 123456789 987654321{{$}}
; AOT-NEXT: heap[0]: 13 26 39 52 65 78 123456789 987654321{{$}}
; AOT-NOT: heap[
; ZERO: feme-run: '--repeat' must be positive
; ZERO-NOT: could not open

;--- shader.ll
target triple = "dxil-unknown-shadermodel6.6-compute"

define void @main() #0 {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  %loaded = call {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison)
  %old = extractvalue {i32, i1} %loaded, 0
  %increment = add i32 %tid, 1
  %next = add i32 %old, %increment
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %next)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare {i32, i1} @llvm.dx.resource.load.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="3,1,1" }

;--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 32
        data: [10, 20, 30, 40, 50, 60, 123456789, 987654321]

;--- object-heap.yaml
resource-heap:
  - index: 0
    size: 32
    data: [10, 20, 30, 40, 50, 60, 123456789, 987654321]
