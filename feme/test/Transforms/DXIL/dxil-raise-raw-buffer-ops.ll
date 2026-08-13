; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's raiseRawBufferStore/raiseRawBufferLoad
; (OpRaising.cpp): a single-component `dx.op.rawBufferStore`/
; `dx.op.rawBufferLoad` (140/139) call consuming an already-raised raw/
; structured buffer handle is rewritten into
; `llvm.dx.resource.store.rawbuffer`/`.load.rawbuffer`, the form a
; `RWStructuredBuffer`/`RWByteAddressBuffer` access in real HLSL (e.g.
; `Out[tid.x] = tid.x;`) lowers to -- see feme/test/Tools/feme-run/HLSL for
; this raised end-to-end through `feme-run`. A multi-component access (mask
; with more than one bit set) is left unraised, matching the scope note in
; each function's own comment.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { ptr }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }

; A `RWStructuredBuffer<uint>` at u0, space 0: `RawBufferStore`'s Coord0/
; Coord1 (element index, byte offset within element) forward straight
; through to the raised intrinsic's own operands unexamined.
; CHECK-LABEL: define void @store_structured(
define void @store_structured(i32 %idx, i32 %val) {
  ; CHECK: [[H:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call void @llvm.dx.resource.store.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx, i32 0, i32 %val)
  ; CHECK-NOT: casthandle
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 %idx, i32 0, i32 %val, i32 poison, i32 poison, i32 poison, i8 1, i32 4)
  ret void
}

; A `RWByteAddressBuffer` at u1, space 0: `RawBufferLoad`'s single-component
; result round-trips completely, leaving no `casthandle` bridge behind.
; CHECK-LABEL: define i32 @load_raw(
define i32 @load_raw(i32 %byteoffset) {
  ; CHECK: [[H:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: [[LOAD:%.*]] = call { i32, i1 } @llvm.dx.resource.load.rawbuffer{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %byteoffset, i32 poison)
  ; CHECK: [[V:%.*]] = extractvalue { i32, i1 } [[LOAD]], 0
  ; CHECK: ret i32 [[V]]
  ; CHECK-NOT: casthandle
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)
  %v = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %h, i32 %byteoffset, i32 poison, i8 1, i32 4)
  %r = extractvalue %dx.types.ResRet.i32 %v, 0
  ret i32 %r
}

; A multi-component store (mask 3, two components) isn't raised: only the
; single-component view `libFeMeRuntimeCPU` implements today is covered (see
; raiseRawBufferStore's own comment) -- left as an unmodified `dx.op.*` call
; rather than mis-raised.
; CHECK-LABEL: define void @store_multi_component(
define void @store_multi_component(i32 %idx, i32 %x, i32 %y) {
  ; CHECK: call void @dx.op.rawBufferStore.i32(i32 140,
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %h, i32 %idx, i32 0, i32 %x, i32 %y, i32 poison, i32 poison, i8 3, i32 4)
  ret void
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare void @dx.op.rawBufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8, i32)
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32)

!dx.resources = !{!0}

!0 = !{null, !1, null, null}
; UAVs: {ID, GV, name, space, lowerBound, rangeSize, kind, coherent, counter,
;        ROV, props}
!1 = !{!2, !3}
!2 = !{i32 0, ptr undef, !"structured", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!3 = !{i32 1, ptr undef, !"raw", i32 0, i32 1, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!4 = !{i32 0, i32 9}
