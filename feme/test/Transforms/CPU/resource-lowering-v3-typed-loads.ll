; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,verify,feme-cpu-lower-resources,verify' -S %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; The descriptor format, not the shader's three-component result, determines
; physical texel size and bounds. Reuse four-component helpers, then keep XYZ.
; CHECK-LABEL: define <3 x float> @float3_load(
; CHECK: [[INDEX:%.*]] = zext i32 %index to i64
; CHECK: [[LOAD:%.*]] = call <4 x float> @feme.cpu.resource.load.typed.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[INDEX]], i1 true)
; CHECK: [[XYZ:%.*]] = shufflevector <4 x float> [[LOAD]], <4 x float> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: ret <3 x float> [[XYZ]]
define <3 x float> @float3_load(i32 %index) {
  %h = call target("dx.TypedBuffer", <3 x float>, 0, 0, 1) @llvm.dx.resource.handlefromheap.f3(i32 0)
  %load = call { <3 x float>, i1 } @llvm.dx.resource.load.typedbuffer.f3(target("dx.TypedBuffer", <3 x float>, 0, 0, 1) %h, i32 %index)
  %value = extractvalue { <3 x float>, i1 } %load, 0
  ret <3 x float> %value
}

; CHECK-LABEL: define <3 x i32> @int3_load(
; CHECK: [[INDEX:%.*]] = zext i32 %index to i64
; CHECK: [[LOAD:%.*]] = call <4 x i32> @feme.cpu.resource.load.typed.v4i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 [[INDEX]], i1 true)
; CHECK: [[XYZ:%.*]] = shufflevector <4 x i32> [[LOAD]], <4 x i32> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: ret <3 x i32> [[XYZ]]
define <3 x i32> @int3_load(i32 %index) {
  %h = call target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap.i3(i32 1)
  %load = call { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) %h, i32 %index)
  %value = extractvalue { <3 x i32>, i1 } %load, 0
  ret <3 x i32> %value
}

; CHECK-LABEL: define <3 x i32> @uint3_load(
; CHECK: [[LOAD:%.*]] = call <4 x i32> @feme.cpu.resource.load.typed.v4i32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 4294967295, i1 true)
; CHECK: [[XYZ:%.*]] = shufflevector <4 x i32> [[LOAD]], <4 x i32> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: ret <3 x i32> [[XYZ]]
define <3 x i32> @uint3_load() {
  %h = call target("dx.TypedBuffer", <3 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap.u3(i32 2)
  %load = call { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.u3(target("dx.TypedBuffer", <3 x i32>, 0, 0, 0) %h, i32 -1)
  %value = extractvalue { <3 x i32>, i1 } %load, 0
  ret <3 x i32> %value
}

; Raw three-component loads retain their exact-width helper.
; CHECK-LABEL: define <3 x float> @raw3_load(
; CHECK: [[LOAD:%.*]] = call <3 x float> @feme.cpu.resource.load.raw.v3f32(
; CHECK-NOT: shufflevector
; CHECK: ret <3 x float> [[LOAD]]
define <3 x float> @raw3_load(i32 %offset) {
  %h = call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32 3)
  %load = call { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer.f3(target("dx.RawBuffer", i8, 0, 0) %h, i32 %offset, i32 0)
  %value = extractvalue { <3 x float>, i1 } %load, 0
  ret <3 x float> %value
}

; Non-32-bit loads retain their existing lowering, without new promotion.
; CHECK-LABEL: define <3 x half> @half3_load(
; CHECK: [[LOAD:%.*]] = call <3 x half> @feme.cpu.resource.load.typed.v3f16(
; CHECK-NOT: shufflevector
; CHECK: ret <3 x half> [[LOAD]]
define <3 x half> @half3_load(i32 %index) {
  %h = call target("dx.TypedBuffer", <3 x half>, 0, 0, 1) @llvm.dx.resource.handlefromheap.h3(i32 4)
  %load = call { <3 x half>, i1 } @llvm.dx.resource.load.typedbuffer.h3(target("dx.TypedBuffer", <3 x half>, 0, 0, 1) %h, i32 %index)
  %value = extractvalue { <3 x half>, i1 } %load, 0
  ret <3 x half> %value
}

; CHECK-NOT: @llvm.dx.resource.
; CHECK-NOT: @feme.cpu.resource.load.typed.v3f32
; CHECK-NOT: @feme.cpu.resource.load.typed.v3i32

declare target("dx.TypedBuffer", <3 x float>, 0, 0, 1) @llvm.dx.resource.handlefromheap.f3(i32)
declare target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap.i3(i32)
declare target("dx.TypedBuffer", <3 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap.u3(i32)
declare target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32)
declare { <3 x float>, i1 } @llvm.dx.resource.load.typedbuffer.f3(target("dx.TypedBuffer", <3 x float>, 0, 0, 1), i32)
declare { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1), i32)
declare { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.u3(target("dx.TypedBuffer", <3 x i32>, 0, 0, 0), i32)
declare { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer.f3(target("dx.RawBuffer", i8, 0, 0), i32, i32)
declare target("dx.TypedBuffer", <3 x half>, 0, 0, 1) @llvm.dx.resource.handlefromheap.h3(i32)
declare { <3 x half>, i1 } @llvm.dx.resource.load.typedbuffer.h3(target("dx.TypedBuffer", <3 x half>, 0, 0, 1), i32)
