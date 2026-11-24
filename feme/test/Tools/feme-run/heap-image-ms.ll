; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/one.yaml %t/shader.ll | FileCheck %s --check-prefix=ONE
; RUN: feme-run --reference --repeat=2 --heap=%t/one.yaml %t/shader.ll | FileCheck %s --check-prefix=ONE
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/two.yaml %t/shader.ll | FileCheck %s --check-prefix=TWO
; RUN: feme-run --reference --repeat=2 --heap=%t/two.yaml %t/shader.ll | FileCheck %s --check-prefix=TWO
; RUN: feme-run --wave-size=8 --repeat=2 --heap=%t/eight.yaml %t/shader.ll | FileCheck %s --check-prefix=EIGHT
; RUN: feme-run --reference --repeat=2 --heap=%t/eight.yaml %t/shader.ll | FileCheck %s --check-prefix=EIGHT
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/one.yaml %t/shader.o | FileCheck %s --check-prefix=ONE
; RUN: feme-run --object --repeat=2 --heap=%t/two.yaml %t/shader.o | FileCheck %s --check-prefix=TWO
; RUN: feme-run --object --repeat=2 --heap=%t/eight.yaml %t/shader.o | FileCheck %s --check-prefix=EIGHT
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Sample -1 through 8, last-row/last-layer array addressing, zero-filled
; unspecified samples and unchanged single-sample defaults.
; ONE: heap[2]: 42 0 0 101 21 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 42
; TWO: heap[2]: 42 0 0 101 21 102 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 42
; EIGHT: heap[2]: 42 0 0 101 21 102 0 103 0 104 0 105 0 106 0 107 0 108 0 0 0 42
;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %sample = sub i32 %tid, 1
  %t = call target("dx.MSTexture", i32, 0, 0, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_0_0_3t(i32 0)
  %a = call target("dx.MSTexture", i32, 0, 0, 0, 8) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_0_0_8t(i32 1)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 2)
  %p = call i32 @llvm.dx.resource.load.ms.i32.tdx.MSTexture_i32_0_0_0_3t.v2i32.v2i32(target("dx.MSTexture", i32, 0, 0, 0, 3) %t, <2 x i32> zeroinitializer, i32 %sample, <2 x i32> zeroinitializer)
  %q = call i32 @llvm.dx.resource.load.ms.i32.tdx.MSTexture_i32_0_0_0_8t.v3i32.v2i32(target("dx.MSTexture", i32, 0, 0, 0, 8) %a, <3 x i32> <i32 1, i32 1, i32 1>, i32 %sample, <2 x i32> zeroinitializer)
  %base0 = mul i32 %tid, 8
  %base = add i32 %base0, 4
  %next = add i32 %base, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %p)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %next, i32 poison, i32 %q)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare target("dx.MSTexture", i32, 0, 0, 0, 3) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_0_0_3t(i32)
declare target("dx.MSTexture", i32, 0, 0, 0, 8) @llvm.dx.resource.handlefromheap.tdx.MSTexture_i32_0_0_0_8t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare i32 @llvm.dx.resource.load.ms.i32.tdx.MSTexture_i32_0_0_0_3t.v2i32.v2i32(target("dx.MSTexture", i32, 0, 0, 0, 3), <2 x i32>, i32, <2 x i32>)
declare i32 @llvm.dx.resource.load.ms.i32.tdx.MSTexture_i32_0_0_0_8t.v3i32.v2i32(target("dx.MSTexture", i32, 0, 0, 0, 8), <3 x i32>, i32, <2 x i32>)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="10,1,1" }
;--- one.yaml
resource-heap:
  - {index: 2, size: 88, data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]}
images:
  - {index: 0, dimension: 2d-ms, extent: [2, 1], format: r32_uint, data: [101, 201]}
  - {index: 1, dimension: 2d-ms-array, extent: [3, 2], array-layers: 2, sample-count: 2, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21]}
;--- two.yaml
resource-heap:
  - {index: 2, size: 88, data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]}
images:
  - {index: 0, dimension: 2d-ms, extent: [2, 1], sample-count: 2, format: r32_uint, data: [101, 102, 201, 202]}
  - {index: 1, dimension: 2d-ms-array, extent: [3, 2], array-layers: 2, sample-count: 2, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21]}
;--- eight.yaml
resource-heap:
  - {index: 2, size: 88, data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]}
images:
  - {index: 0, dimension: 2d-ms, extent: [2, 1], sample-count: 8, format: r32_uint, data: [101, 102, 103, 104, 105, 106, 107, 108, 201, 202, 203, 204, 205, 206, 207, 208]}
  - {index: 1, dimension: 2d-ms-array, extent: [3, 2], array-layers: 2, sample-count: 2, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21]}
