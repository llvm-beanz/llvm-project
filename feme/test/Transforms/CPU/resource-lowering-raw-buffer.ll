; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s

; Covers feme::cpu::ResourceLoweringPass's canonicalization of the two
; bindless raw-buffer shapes it handles through the same
; `feme.cpu.resource.*.raw.*` family (see "Descriptor heaps" in
; feme/docs/FeMeCPUDesign.md): an unstructured `ByteAddressBuffer`, whose
; index operand is already a byte address, and a `StructuredBuffer`, whose
; two index operands (element index, byte offset within the element) this
; pass combines into one byte offset using the element type's store size as
; the stride.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @byte_address_buffer(
; CHECK: [[OFF:%.*]] = zext i32 %byte_offset to i64
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(
; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 5, i64 [[OFF]], i1 true)
define void @byte_address_buffer(i32 %byte_offset) {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32 5, i1 false)
  %loaded = call {i32, i1}
      @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(
          target("dx.RawBuffer", i8, 1, 0) %h, i32 %byte_offset, i32 poison)
  %val = extractvalue {i32, i1} %loaded, 0
  ret void
}

; A `StructuredBuffer<float4>` (16-byte stride) at element index 2, byte
; offset 4 within the element: canonical byte offset is 2*16 + 4 = 36
; (constant-folded by the pass's own IRBuilder since the operands here are
; constants).
; CHECK-LABEL: define void @structured_buffer(
; CHECK: call float @feme.cpu.resource.load.raw.f32(
; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 9, i64 36, i1 true)
define void @structured_buffer() {
  %h = call target("dx.RawBuffer", [16 x i8], 1, 0)
      @llvm.dx.resource.handlefromheap.tdx.RawBuffer_a16i8_1_0t(i32 9, i1 false)
  %loaded = call {float, i1}
      @llvm.dx.resource.load.rawbuffer.f32.tdx.RawBuffer_a16i8_1_0t(
          target("dx.RawBuffer", [16 x i8], 1, 0) %h, i32 2, i32 4)
  %val = extractvalue {float, i1} %loaded, 0
  ret void
}

declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_i8_1_0t(i32, i1)
declare {i32, i1}
    @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(
        target("dx.RawBuffer", i8, 1, 0), i32, i32)

declare target("dx.RawBuffer", [16 x i8], 1, 0)
    @llvm.dx.resource.handlefromheap.tdx.RawBuffer_a16i8_1_0t(i32, i1)
declare {float, i1}
    @llvm.dx.resource.load.rawbuffer.f32.tdx.RawBuffer_a16i8_1_0t(
        target("dx.RawBuffer", [16 x i8], 1, 0), i32, i32)
