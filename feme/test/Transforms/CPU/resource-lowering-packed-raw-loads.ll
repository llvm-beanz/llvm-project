; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes='feme-cpu-lower-resources,verify,feme-cpu-lower-resources,verify' -S %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @packed_loads(
; CHECK: [[RAW:%.*]] = zext i32 %offset to i64
; CHECK: call <2 x i32> @feme.cpu.resource.load.raw.v2i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 [[RAW]], i1 true)
; CHECK: [[I:%.*]] = zext i32 %index to i64
; CHECK: [[M:%.*]] = mul i64 [[I]], 20
; CHECK: [[A:%.*]] = add i64 [[M]], 4
; CHECK: call <4 x i32> @feme.cpu.resource.load.raw.v4i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 [[A]], i1 true)
; CHECK: [[FI:%.*]] = zext i32 %index to i64
; CHECK: [[FM:%.*]] = mul i64 [[FI]], 16
; CHECK: [[FA:%.*]] = add i64 [[FM]], 4
; CHECK: call <3 x float> @feme.cpu.resource.load.raw.v3f32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 [[FA]], i1 true)
; CHECK-NOT: call {{.*}} @llvm.dx.resource.
define void @packed_loads(i32 %index, i32 %offset) {
  %raw = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.raw(i32 0)
  %ints = call target("dx.RawBuffer", [20 x i8], 1, 0) @llvm.dx.resource.handlefromheap.ints(i32 1)
  %floats = call target("dx.RawBuffer", [16 x i8], 1, 0) @llvm.dx.resource.handlefromheap.floats(i32 2)
  %a = call { <2 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v2i32(target("dx.RawBuffer", i8, 1, 0) %raw, i32 %offset, i32 poison)
  %av = extractvalue { <2 x i32>, i1 } %a, 0
  %b = call { <4 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v4i32(target("dx.RawBuffer", [20 x i8], 1, 0) %ints, i32 %index, i32 4)
  %bv = extractvalue { <4 x i32>, i1 } %b, 0
  %c = call { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer.v3f32(target("dx.RawBuffer", [16 x i8], 1, 0) %floats, i32 %index, i32 4)
  %cv = extractvalue { <3 x float>, i1 } %c, 0
  ret void
}

declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap.raw(i32)
declare target("dx.RawBuffer", [20 x i8], 1, 0) @llvm.dx.resource.handlefromheap.ints(i32)
declare target("dx.RawBuffer", [16 x i8], 1, 0) @llvm.dx.resource.handlefromheap.floats(i32)
declare { <2 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v2i32(target("dx.RawBuffer", i8, 1, 0), i32, i32)
declare { <4 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v4i32(target("dx.RawBuffer", [20 x i8], 1, 0), i32, i32)
declare { <3 x float>, i1 } @llvm.dx.resource.load.rawbuffer.v3f32(target("dx.RawBuffer", [16 x i8], 1, 0), i32, i32)
