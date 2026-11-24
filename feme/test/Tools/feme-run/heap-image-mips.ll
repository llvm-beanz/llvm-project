; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --wave-size=8 --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: feme-run --reference --repeat=2 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s
; RUN: llvm-as %t/shader.ll -o %t/shader.bc
; RUN: feme --wave-size=4 --target=%feme_host_triple %t/shader.bc -o %t/shader.o
; RUN: feme-run --object --repeat=2 --heap=%t/heap.yaml %t/shader.o | FileCheck %s
; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
; See https://llvm.org/LICENSE.txt for license information.
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
; Rectangular non-power-of-two mips, fixed array layers, shrinking 3D depth,
; the mip-31 boundary, zero-filled trailing data, guards and partial waves.
; CHECK: heap[6]: 42 0 0 0 0 0 32 0 105 25 15 35 45 32 0 202 42 102 202 102 32 0 301 52 201 0 201 32 0 0 0 0 0 0 0 0 42
;--- shader.ll
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %mip = sub i32 %tid, 1
  %zero = icmp eq i32 %mip, 0
  %one = icmp eq i32 %mip, 1
  %smallx = select i1 %one, i32 1, i32 0
  %x = select i1 %zero, i32 4, i32 %smallx
  %y = select i1 %zero, i32 2, i32 0
  %xy0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %xy = insertelement <2 x i32> %xy0, i32 %y, i32 1
  %xa = insertelement <2 x i32> %xy0, i32 1, i32 1
  %xyz0 = shufflevector <2 x i32> %xy, <2 x i32> poison, <3 x i32> <i32 0, i32 1, i32 poison>
  %xya = insertelement <3 x i32> %xyz0, i32 1, i32 2
  %xyz = insertelement <3 x i32> %xyz0, i32 %y, i32 2
  %upperz = add i32 %y, 1
  %badxyz = insertelement <3 x i32> %xyz0, i32 %upperz, i32 2
  %last = icmp eq i32 %tid, 4
  %lastmip = select i1 %last, i32 32, i32 31
  %t1 = call target("dx.Texture", i32, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_1t(i32 0)
  %a1 = call target("dx.Texture", i32, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_6t(i32 1)
  %t2 = call target("dx.Texture", i32, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_2t(i32 2)
  %a2 = call target("dx.Texture", i32, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_7t(i32 3)
  %t3 = call target("dx.Texture", i32, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_4t(i32 4)
  %t32 = call target("dx.Texture", i32, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_1t(i32 5)
  %out = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 6)
  %r1 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_1t.i32.i32.i32(target("dx.Texture", i32, 0, 0, 0, 1) %t1, i32 %x, i32 %mip, i32 0)
  %ra1 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_6t.v2i32.i32.i32(target("dx.Texture", i32, 0, 0, 0, 6) %a1, <2 x i32> %xa, i32 %mip, i32 0)
  %r2 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 2) %t2, <2 x i32> %xy, i32 %mip, <2 x i32> zeroinitializer)
  %ra2 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 7) %a2, <3 x i32> %xya, i32 %mip, <2 x i32> zeroinitializer)
  %r3 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", i32, 0, 0, 0, 4) %t3, <3 x i32> %xyz, i32 %mip, <3 x i32> zeroinitializer)
  %r32 = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_1t.i32.i32.i32(target("dx.Texture", i32, 0, 0, 0, 1) %t32, i32 0, i32 %lastmip, i32 0)
  %bad = call i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", i32, 0, 0, 0, 4) %t3, <3 x i32> %badxyz, i32 %mip, <3 x i32> zeroinitializer)
  %base0 = mul i32 %tid, 28
  %base = add i32 %base0, 4
  %b1 = add i32 %base, 4
  %b2 = add i32 %base, 8
  %b3 = add i32 %base, 12
  %b4 = add i32 %base, 16
  %b5 = add i32 %base, 20
  %b6 = add i32 %base, 24
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %base, i32 poison, i32 %r1)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b1, i32 poison, i32 %ra1)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b2, i32 poison, i32 %r2)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b3, i32 poison, i32 %ra2)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b4, i32 poison, i32 %r3)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b5, i32 poison, i32 %r32)
  call void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0) %out, i32 %b6, i32 poison, i32 %bad)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare target("dx.Texture", i32, 0, 0, 0, 1) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_1t(i32)
declare target("dx.Texture", i32, 0, 0, 0, 6) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_6t(i32)
declare target("dx.Texture", i32, 0, 0, 0, 2) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_2t(i32)
declare target("dx.Texture", i32, 0, 0, 0, 7) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_7t(i32)
declare target("dx.Texture", i32, 0, 0, 0, 4) @llvm.dx.resource.handlefromheap.tdx.Texture_i32_0_0_0_4t(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_1t.i32.i32.i32(target("dx.Texture", i32, 0, 0, 0, 1), i32, i32, i32)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_6t.v2i32.i32.i32(target("dx.Texture", i32, 0, 0, 0, 6), <2 x i32>, i32, i32)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_2t.v2i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 2), <2 x i32>, i32, <2 x i32>)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_7t.v3i32.i32.v2i32(target("dx.Texture", i32, 0, 0, 0, 7), <3 x i32>, i32, <2 x i32>)
declare i32 @llvm.dx.resource.load.level.i32.tdx.Texture_i32_0_0_0_4t.v3i32.i32.v3i32(target("dx.Texture", i32, 0, 0, 0, 4), <3 x i32>, i32, <3 x i32>)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="5,1,1" }
;--- heap.yaml
resource-heap:
  - index: 6
    size: 148
    data: [42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42, 42]
images:
  - {index: 0, dimension: 1d, extent: [5], mip-levels: 3, format: r32_uint, data: [101, 102, 103, 104, 105, 201, 202, 301]}
  - {index: 1, dimension: 1d-array, extent: [5], array-layers: 2, mip-levels: 3, format: r32_uint, data: [11, 12, 13, 14, 15, 21, 22, 23, 24, 25, 31, 32, 41, 42, 51, 52]}
  - {index: 2, dimension: 2d, extent: [5, 3], mip-levels: 3, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 101, 102, 201]}
  - {index: 3, dimension: 2d-array, extent: [5, 3], array-layers: 2, mip-levels: 3, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 101, 102, 201, 202, 301]}
  - {index: 4, dimension: 3d, extent: [5, 3, 3], mip-levels: 3, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 101, 102, 201]}
  - {index: 5, dimension: 1d, extent: [1], mip-levels: 32, format: r32_uint, data: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]}
