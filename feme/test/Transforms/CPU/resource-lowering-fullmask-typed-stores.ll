; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,verify,feme-cpu-lower-resources,verify' -S %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; Narrow shader handles must not narrow four-channel write payloads.
; CHECK-LABEL: define void @fullmask_stores(
; CHECK: [[I:%.*]] = zext i32 %index to i64
; CHECK: call void @feme.cpu.resource.store.typed.v4i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[I]], <4 x i32> %ints, i1 true)
; CHECK: [[F:%.*]] = zext i32 %index to i64
; CHECK: call void @feme.cpu.resource.store.typed.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 [[F]], <4 x float> %floats, i1 true)
; CHECK-NOT: call {{.*}} @llvm.dx.resource.
define void @fullmask_stores(i32 %index, <4 x i32> %ints, <4 x float> %floats) {
  %hi = call target("dx.TypedBuffer", i32, 1, 0, 0) @llvm.dx.resource.handlefromheap.i32(i32 0)
  %hf = call target("dx.TypedBuffer", <2 x float>, 1, 0, 1) @llvm.dx.resource.handlefromheap.v2f32(i32 1)
  call void @llvm.dx.resource.store.typedbuffer.i32.v4i32(target("dx.TypedBuffer", i32, 1, 0, 0) %hi, i32 %index, <4 x i32> %ints)
  call void @llvm.dx.resource.store.typedbuffer.v2f32.v4f32(target("dx.TypedBuffer", <2 x float>, 1, 0, 1) %hf, i32 %index, <4 x float> %floats)
  ret void
}

declare target("dx.TypedBuffer", i32, 1, 0, 0) @llvm.dx.resource.handlefromheap.i32(i32)
declare target("dx.TypedBuffer", <2 x float>, 1, 0, 1) @llvm.dx.resource.handlefromheap.v2f32(i32)
declare void @llvm.dx.resource.store.typedbuffer.i32.v4i32(target("dx.TypedBuffer", i32, 1, 0, 0), i32, <4 x i32>)
declare void @llvm.dx.resource.store.typedbuffer.v2f32.v4f32(target("dx.TypedBuffer", <2 x float>, 1, 0, 1), i32, <4 x float>)
