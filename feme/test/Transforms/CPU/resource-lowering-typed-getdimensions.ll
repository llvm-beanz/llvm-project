; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,verify,feme-cpu-lower-resources,verify' -S %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; Unsupported functions stay ahead of the rewritten functions in the module.
; CHECK-LABEL: define i32 @raw_dimensions()
; CHECK: call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap{{.*}}
; CHECK: call i32 @llvm.dx.resource.getdimensions.x{{.*}}
define i32 @raw_dimensions() {
  %h = call target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32 0)
  %n = call i32 @llvm.dx.resource.getdimensions.x.raw(target("dx.RawBuffer", i8, 0, 0) %h)
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

declare target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap.f32(i32)
declare target("dx.TypedBuffer", <3 x i32>, 1, 0, 1) @llvm.dx.resource.handlefromheap.i3(i32)
declare target("dx.TypedBuffer", <4 x i32>, 0, 0, 0) @llvm.dx.resource.handlefromheap.u4(i32)
declare target("dx.RawBuffer", i8, 0, 0) @llvm.dx.resource.handlefromheap.raw(i32)
declare i32 @llvm.dx.resource.getdimensions.x.f32(target("dx.TypedBuffer", float, 0, 0, 1))
declare i32 @llvm.dx.resource.getdimensions.x.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1))
declare i32 @llvm.dx.resource.getdimensions.x.u4(target("dx.TypedBuffer", <4 x i32>, 0, 0, 0))
declare i32 @llvm.dx.resource.getdimensions.x.raw(target("dx.RawBuffer", i8, 0, 0))
declare <2 x i32> @llvm.dx.resource.getdimensions.xy.f32(target("dx.TypedBuffer", float, 0, 0, 1))
declare { <3 x i32>, i1 } @llvm.dx.resource.load.typedbuffer.i3(target("dx.TypedBuffer", <3 x i32>, 1, 0, 1), i32)
