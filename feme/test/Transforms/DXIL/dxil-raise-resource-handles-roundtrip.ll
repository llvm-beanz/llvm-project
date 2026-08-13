; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops -S | FileCheck %s

; End-to-end validation that raiseResourceHandleFromBinding (OpRaising.cpp)
; is a genuine inverse of the real `DXILOpLowering::lowerToBindAndAnnotateHandle`
; (llvm/lib/Target/DirectX/DXILOpLowering.cpp): starts from the pre-lowering
; `llvm.dx.resource.handlefrombinding`/`llvm.dx.resource.load.*` intrinsic
; calls a real DXIL-targeting frontend would emit, lowers them with the real
; `-dxil-op-lower` pass, then raises the handle-creation part back and checks
; it reconstructs the original resource binding, and (for `TypedBuffer`/
; unstructured `RawBuffer`) the exact original handle type, or (for
; `StructuredBuffer`/`CBuffer`) a same-size/alignment opaque placeholder
; handle type -- see `raiseResourceHandleFromBinding`'s comment for why. A
; typed buffer load (`dx.op.bufferLoad`) and a single-component raw buffer
; load (`dx.op.rawBufferLoad`) both round-trip completely, leaving no
; `llvm.dx.resource.casthandle` bridge behind (see `raiseRawBufferLoad`'s own
; comment in OpRaising.cpp for what's covered). The structured buffer/cbuffer
; *load* calls (`dx.op.rawBufferLoad` on a struct-typed element,
; `dx.op.cbufferLoadLegacy`) are left as-is -- raising an aggregate-typed
; load isn't implemented yet, see the DXIL section of feme/docs/Design.md --
; so for those this only checks the handle sequence, not a full round-trip of
; the whole function.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

%struct.S = type { float, <4 x i32> }

@ResName1 = private unnamed_addr constant [4 x i8] c"buf\00"
@ResName2 = private unnamed_addr constant [4 x i8] c"raw\00"
@ResName3 = private unnamed_addr constant [3 x i8] c"sb\00"
@ResName4 = private unnamed_addr constant [3 x i8] c"cb\00"

; A `Buffer<float>` (SRV TypedBuffer) bound at register t1, space 0.
; CHECK-LABEL: define float @typed_buffer_srv(
define float @typed_buffer_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK-NEXT: [[LOAD:%.*]] = call { float, i1 } @llvm.dx.resource.load.typedbuffer{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[HANDLE]], i32 %idx)
  ; CHECK-NEXT: extractvalue { float, i1 } [[LOAD]], 0
  ; CHECK-NOT: casthandle
  %h = call target("dx.TypedBuffer", float, 0, 0, 1)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_f32_0_0_1t(i32 0, i32 1, i32 1, i32 0, ptr @ResName1)
  %v = call {float, i1} @llvm.dx.resource.load.typedbuffer.f32.tdx.TypedBuffer_f32_0_0_1t(target("dx.TypedBuffer", float, 0, 0, 1) %h, i32 %idx)
  %r = extractvalue {float, i1} %v, 0
  ret float %r
}

; A `RWByteAddressBuffer` (UAV, unstructured RawBuffer) bound at register u0:
; a single-component raw buffer load round-trips completely too (see
; `raiseRawBufferLoad`), the same as the typed buffer case above.
; CHECK-LABEL: define i32 @raw_buffer_uav(
define i32 @raw_buffer_uav(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK-NEXT: [[LOAD:%.*]] = call { i32, i1 } @llvm.dx.resource.load.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[HANDLE]], i32 %idx, i32 undef)
  ; CHECK-NEXT: extractvalue { i32, i1 } [[LOAD]], 0
  ; CHECK-NOT: casthandle
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 1, i32 0, ptr @ResName2)
  %v = call {i32, i1} @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(target("dx.RawBuffer", i8, 1, 0) %h, i32 %idx, i32 poison)
  %r = extractvalue {i32, i1} %v, 0
  ret i32 %r
}

; A `StructuredBuffer<S>` (SRV) bound at register t2, where `S`'s largest
; member is a `<4 x i32>` (align 16): its original field layout isn't
; recoverable from binding metadata alone, so the raised handle's element
; type is an opaque size/alignment-only placeholder (`getOpaqueSizedType`),
; not `%struct.S` itself -- see that function's comment.
; CHECK-LABEL: define void @structured_buffer_srv(
define void @structured_buffer_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", { <4 x i32>, [16 x i8] }, 0, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.RawBuffer", { <4 x i32>, [16 x i8] }, 0, 0) [[HANDLE]])
  %h = call target("dx.RawBuffer", %struct.S, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_s_struct.Ss_0_0t(i32 0, i32 2, i32 1, i32 0, ptr @ResName3)
  %v = call {%struct.S, i1} @llvm.dx.resource.load.rawbuffer.s_struct.Ss.tdx.RawBuffer_s_struct.Ss_0_0t(target("dx.RawBuffer", %struct.S, 0, 0) %h, i32 %idx, i32 0)
  ret void
}

; A `cbuffer` bound at register b2: its `ResourceProperties` encoding never
; carries alignment bits, so its opaque placeholder element type is always a
; plain byte array (`getOpaqueSizedType`).
; CHECK-LABEL: define void @cbuffer_case(
define void @cbuffer_case(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.CBuffer", [32 x i8]) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.CBuffer", [32 x i8]) [[HANDLE]])
  %h = call target("dx.CBuffer", %struct.S)
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_s_struct.Ss_t(i32 0, i32 2, i32 1, i32 0, ptr @ResName4)
  %v = call {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_s_struct.Ss_t(target("dx.CBuffer", %struct.S) %h, i32 %idx)
  ret void
}

declare target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_f32_0_0_1t(i32, i32, i32, i32, ptr)
declare {float, i1} @llvm.dx.resource.load.typedbuffer.f32.tdx.TypedBuffer_f32_0_0_1t(target("dx.TypedBuffer", float, 0, 0, 1), i32)
declare target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t(i32, i32, i32, i32, ptr)
declare {i32, i1} @llvm.dx.resource.load.rawbuffer.i32.tdx.RawBuffer_i8_1_0t(target("dx.RawBuffer", i8, 1, 0), i32, i32)
declare target("dx.RawBuffer", %struct.S, 0, 0) @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_s_struct.Ss_0_0t(i32, i32, i32, i32, ptr)
declare {%struct.S, i1} @llvm.dx.resource.load.rawbuffer.s_struct.Ss.tdx.RawBuffer_s_struct.Ss_0_0t(target("dx.RawBuffer", %struct.S, 0, 0), i32, i32)
declare target("dx.CBuffer", %struct.S) @llvm.dx.resource.handlefrombinding.tdx.CBuffer_s_struct.Ss_t(i32, i32, i32, i32, ptr)
declare {i32, i32, i32, i32} @llvm.dx.resource.load.cbufferrow.4.i32.tdx.CBuffer_s_struct.Ss_t(target("dx.CBuffer", %struct.S), i32)
