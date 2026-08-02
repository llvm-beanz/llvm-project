; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops -S | FileCheck %s

; End-to-end validation that raiseResourceHandleFromBinding (OpRaising.cpp)
; is a genuine inverse of the real `DXILOpLowering::lowerToBindAndAnnotateHandle`
; (llvm/lib/Target/DirectX/DXILOpLowering.cpp): starts from the pre-lowering
; `llvm.dx.resource.handlefrombinding`/`llvm.dx.resource.load.*` intrinsic
; calls a real DXIL-targeting frontend would emit, lowers them with the real
; `-dxil-op-lower` pass, then raises the handle-creation part back and checks
; it reconstructs the original resource handle type and binding. The buffer
; *load* calls (`dx.op.bufferLoad`/`dx.op.rawBufferLoad`) are left as-is --
; raising those isn't implemented yet, see the DXIL section of
; feme/docs/Design.md -- so this only checks the handle sequence, not a full
; round-trip of the whole function.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

@ResName1 = private unnamed_addr constant [4 x i8] c"buf\00"
@ResName2 = private unnamed_addr constant [4 x i8] c"raw\00"

; A `Buffer<float>` (SRV TypedBuffer) bound at register t1, space 0.
; CHECK-LABEL: define float @typed_buffer_srv(
define float @typed_buffer_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[HANDLE]])
  %h = call target("dx.TypedBuffer", float, 0, 0, 1)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_f32_0_0_1t(i32 0, i32 1, i32 1, i32 0, ptr @ResName1)
  %v = call {float, i1} @llvm.dx.resource.load.typedbuffer.f32.tdx.TypedBuffer_f32_0_0_1t(target("dx.TypedBuffer", float, 0, 0, 1) %h, i32 %idx)
  %r = extractvalue {float, i1} %v, 0
  ret float %r
}

; A `RWByteAddressBuffer` (UAV, unstructured RawBuffer) bound at register u0.
; CHECK-LABEL: define i32 @raw_buffer_uav(
define i32 @raw_buffer_uav(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[HANDLE]])
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 1, i32 0, ptr @ResName2)
  %v = call {i32, i1} @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(target("dx.RawBuffer", i8, 1, 0) %h, i32 %idx, i32 poison)
  %r = extractvalue {i32, i1} %v, 0
  ret i32 %r
}

declare target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_f32_0_0_1t(i32, i32, i32, i32, ptr)
declare {float, i1} @llvm.dx.resource.load.typedbuffer.f32.tdx.TypedBuffer_f32_0_0_1t(target("dx.TypedBuffer", float, 0, 0, 1), i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32, i32, i32, i32, ptr)
declare {i32, i1} @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(target("dx.RawBuffer", i8, 1, 0), i32, i32)
