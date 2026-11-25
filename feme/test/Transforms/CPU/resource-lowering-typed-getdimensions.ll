; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,verify,feme-cpu-lower-resources,verify' -S %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; Unsupported functions stay ahead of the rewritten functions in the module.
; CHECK-LABEL: define i32 @zero_stride_dimensions()
; CHECK: call target("dx.RawBuffer", [0 x i8], 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: call i32 @llvm.dx.resource.getdimensions.x{{.*}}
define i32 @zero_stride_dimensions() {
  %h = call target("dx.RawBuffer", [0 x i8], 0, 0) @llvm.dx.resource.handlefromheap.zero(i32 0)
  %n = call i32 @llvm.dx.resource.getdimensions.x.zero(target("dx.RawBuffer", [0 x i8], 0, 0) %h)
  ret i32 %n
}

; CHECK-LABEL: define <2 x i32> @typed_xy()
; CHECK: call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.xy{{.*}}
define <2 x i32> @typed_xy() {
  %h = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap.f32(i32 0)
  %n = call <2 x i32> @llvm.dx.resource.getdimensions.xy.f32(target("dx.TypedBuffer", float, 0, 0, 1) %h)
  ret <2 x i32> %n
}

; CHECK-LABEL: define <2 x i32> @raw_xy()
; CHECK: call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.xy{{.*}}
define <2 x i32> @raw_xy() {
  %h = call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32 0)
  %n = call <2 x i32> @llvm.dx.resource.getdimensions.xy.raw(target("dx.RawBuffer", i8, 0, 0) %h)
  ret <2 x i32> %n
}

; CHECK-LABEL: define <2 x i32> @structured_xy()
; CHECK: call target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: call <2 x i32> @llvm.dx.resource.getdimensions.xy{{.*}}
define <2 x i32> @structured_xy() {
  %h = call target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefromheap.structured(i32 0)
  %n = call <2 x i32> @llvm.dx.resource.getdimensions.xy.structured(target("dx.RawBuffer", [12 x i8], 0, 0) %h)
  ret <2 x i32> %n
}

; Dimensions use descriptor metadata, with no element index or shader-width stride.
; CHECK-LABEL: define i32 @scalar_dimensions(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 %index, i1 true)
; CHECK-NEXT: ret i32 [[N]]
define i32 @scalar_dimensions(i32 %index) {
  %h = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap.f32(i32 %index)
  %n = call i32 @llvm.dx.resource.getdimensions.x.f32(target("dx.TypedBuffer", float, 0, 0, 1) %h)
  ret i32 %n
}

; Queries and accesses through the same handle must lower together.
; CHECK-LABEL: define i32 @dimensions_and_load(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i1 true)
; CHECK: [[V:%.*]] = call <4 x i32> @feme.cpu.resource.load.typed.v4i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 2, i1 true)
; CHECK: shufflevector <4 x i32> [[V]], <4 x i32> poison, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK: add i32 [[N]],
define i32 @dimensions_and_load() {
  %h = call target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap.i3(i32 1)
  %n = call i32 @llvm.dx.resource.getdimensions.x.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) %h)
  %load = call { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) %h, i32 2)
  %v = extractvalue { <3 x i32>, i1 } %load, 0
  %x = extractelement <3 x i32> %v, i32 0
  %sum = add i32 %n, %x
  ret i32 %sum
}

; CHECK-LABEL: define i32 @unsigned_dimensions(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 -1, i1 true)
; CHECK-NEXT: ret i32 [[N]]
define i32 @unsigned_dimensions() {
  %h = call target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap.u4(i32 -1)
  %n = call i32 @llvm.dx.resource.getdimensions.x.u4(target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) %h)
  ret i32 %n
}

; Byte-address queries use stride one and prefix zero, never a DWORD divisor.
; CHECK-LABEL: define i32 @raw_dimensions(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 %index, i64 1, i64 0, i1 true)
; CHECK-NEXT: ret i32 [[N]]
define i32 @raw_dimensions(i32 %index) {
  %h = call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32 %index)
  %n = call i32 @llvm.dx.resource.getdimensions.x.raw(target("dx.RawBuffer", i8, 0, 0) %h)
  ret i32 %n
}

; CHECK-LABEL: define i32 @raw_uav_dimensions(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 -1, i64 1, i64 0, i1 true)
; CHECK-NEXT: ret i32 [[N]]
define i32 @raw_uav_dimensions() {
  %h = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.uavraw(i32 -1)
  %n = call i32 @llvm.dx.resource.getdimensions.x.uavraw(target("dx.RawBuffer", i8, 1, 0) %h)
  ret i32 %n
}

; CHECK-LABEL: define i32 @structured_dimensions(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 %index, i64 12, i64 0, i1 true)
; CHECK-NEXT: ret i32 [[N]]
define i32 @structured_dimensions(i32 %index) {
  %h = call target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefromheap.structured(i32 %index)
  %n = call i32 @llvm.dx.resource.getdimensions.x.structured(target("dx.RawBuffer", [12 x i8], 0, 0) %h)
  ret i32 %n
}

; A query and access share the same declared structure stride.
; CHECK-LABEL: define i32 @structured_dimensions_and_load(
; CHECK: [[N:%.*]] = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 20, i64 0, i1 true)
; CHECK: [[V:%.*]] = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 24, i1 true)
; CHECK: add i32 [[N]], [[V]]
define i32 @structured_dimensions_and_load() {
  %h = call target("dx.RawBuffer", [20 x i8], 1, 0) @llvm.dx.resource.handlefromheap.structured_uav(i32 1)
  %n = call i32 @llvm.dx.resource.getdimensions.x.structured_uav(target("dx.RawBuffer", [20 x i8], 1, 0) %h)
  %load = call { i32, i1 } @llvm.dx.resource.load.rawbuffer.structured_uav(target("dx.RawBuffer", [20 x i8], 1, 0) %h, i32 1, i32 4)
  %v = extractvalue { i32, i1 } %load, 0
  %sum = add i32 %n, %v
  ret i32 %sum
}

declare target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap.f32(i32)
declare target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap.i3(i32)
declare target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap.u4(i32)
declare target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.uavraw(i32)
declare target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefromheap.structured(i32)
declare target("dx.RawBuffer", [20 x i8], 1, 0) @llvm.dx.resource.handlefromheap.structured_uav(i32)
declare target("dx.RawBuffer", [0 x i8], 0, 0) @llvm.dx.resource.handlefromheap.zero(i32)
declare i32 @llvm.dx.resource.getdimensions.x.f32(target("dx.TypedBuffer", float, 0, 0, 1))
declare i32 @llvm.dx.resource.getdimensions.x.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1))
declare i32 @llvm.dx.resource.getdimensions.x.u4(target("dx.TypedBuffer", <4 x i32>, 0, 0, 0))
declare i32 @llvm.dx.resource.getdimensions.x.raw(target("dx.RawBuffer", i8, 0, 0))
declare i32 @llvm.dx.resource.getdimensions.x.uavraw(target("dx.RawBuffer", i8, 1, 0))
declare i32 @llvm.dx.resource.getdimensions.x.structured(target("dx.RawBuffer", [12 x i8], 0, 0))
declare i32 @llvm.dx.resource.getdimensions.x.structured_uav(target("dx.RawBuffer", [20 x i8], 1, 0))
declare i32 @llvm.dx.resource.getdimensions.x.zero(target("dx.RawBuffer", [0 x i8], 0, 0))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.structured(target("dx.RawBuffer", [12 x i8], 0, 0))
declare { i32, i1 } @llvm.dx.resource.load.rawbuffer.structured_uav(target("dx.RawBuffer", [20 x i8], 1, 0), i32, i32)
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.raw(target("dx.RawBuffer", i8, 0, 0))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.f32(target("dx.TypedBuffer", float, 0, 0, 1))
declare { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1), i32)
