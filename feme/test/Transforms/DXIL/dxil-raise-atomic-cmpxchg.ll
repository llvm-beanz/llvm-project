; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's raiseAtomicCompareExchange
; (OpRaising.cpp): a `dx.op.atomicCompareExchange` (79) call consuming an
; already-raised raw/typed buffer handle is rewritten into
; `llvm.dx.resource.getpointer` plus a plain `cmpxchg`, the
; `InterlockedCompareExchange` counterpart to dxil-raise-atomic-binop.ll's
; `raiseAtomicBinOp` coverage. See
; feme/test/Tools/feme-run/HLSL/resource-atomic-compare-exchange.hlsl for
; this raised end-to-end through `feme-run`.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { ptr }

; A `RWStructuredBuffer<uint>`-shaped `dx.RawBuffer` UAV at u0, space 0.
; Unlike `dx.op.atomicBinOp`, the DXIL op itself returns only the original
; value directly (no `BinOpCode`-style opcode operand, and no way to
; express `cmpxchg`'s own success flag) -- raising still has to build the
; `{T, i1}`-returning `cmpxchg` `llvm.dx.resource.getpointer`'s consumers
; elsewhere already expect, so it synthesizes its own `extractvalue ..., 0`
; immediately and replaces the call's uses with that extract, not the raw
; `cmpxchg` result.
; CHECK-LABEL: define i32 @atomic_compare_exchange(
define i32 @atomic_compare_exchange(i32 %idx, i32 %cmp, i32 %val) {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  ; CHECK: [[P:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) %{{.*}}, i32 %idx)
  ; CHECK: [[CX:%.*]] = cmpxchg ptr [[P]], i32 %cmp, i32 %val monotonic monotonic
  ; CHECK: [[ORIG:%.*]] = extractvalue { i32, i1 } [[CX]], 0
  ; CHECK-NOT: casthandle
  %old = call i32 @dx.op.atomicCompareExchange.i32(i32 79, %dx.types.Handle %h, i32 %idx, i32 poison, i32 poison, i32 %cmp, i32 %val)
  ; CHECK: ret i32 [[ORIG]]
  ret i32 %old
}

; A genuine `StructuredBuffer`-of-a-struct `cmpxchg` carries both a struct
; index (Coord0) and an intra-element byte offset (Coord1): raising adds a
; byte-level `getelementptr` off the `getpointer` result for that offset,
; exactly mirroring `raiseAtomicBinOp`'s own `atomic_struct_offset` case.
; CHECK-LABEL: define i32 @atomic_compare_exchange_struct_offset(
define i32 @atomic_compare_exchange_struct_offset(i32 %idx, i32 %suboffset, i32 %cmp, i32 %val) {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)
  ; CHECK: [[P:%.*]] = call ptr @llvm.dx.resource.getpointer{{.*}}(target("dx.RawBuffer", i8, 1, 0) %{{.*}}, i32 %idx)
  ; CHECK: [[GEP:%.*]] = getelementptr i8, ptr [[P]], i32 %suboffset
  ; CHECK: [[CX:%.*]] = cmpxchg ptr [[GEP]], i32 %cmp, i32 %val monotonic monotonic
  ; CHECK: extractvalue { i32, i1 } [[CX]], 0
  %old = call i32 @dx.op.atomicCompareExchange.i32(i32 79, %dx.types.Handle %h, i32 %idx, i32 %suboffset, i32 poison, i32 %cmp, i32 %val)
  ret i32 %old
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare i32 @dx.op.atomicCompareExchange.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)

!dx.resources = !{!0}

!0 = !{null, !1, null, null}
; UAVs: {ID, GV, name, space, lowerBound, rangeSize, kind, coherent, counter,
;        ROV, props}
!1 = !{!2, !3}
!2 = !{i32 0, ptr undef, !"structured", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!3 = !{i32 1, ptr undef, !"raw", i32 0, i32 1, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!4 = !{i32 0, i32 9}
