; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's raiseAtomicBinOp (OpRaising.cpp): a
; `dx.op.atomicBinOp` (78) call consuming an already-raised raw/typed
; buffer handle is rewritten into `llvm.dx.resource.getpointer` plus a
; plain `atomicrmw` -- the same "idiomatic LLVM IR" shape Clang's own DXIL
; frontend emits for a resource (not groupshared) `InterlockedAdd`/atomic
; *before* a real DXIL backend ever converts it into this literal DXIL op
; (see llvm/test/CodeGen/DirectX/ResourceAtomicBinOp.ll for that forward
; direction), and the one `feme::cpu::ResourceLoweringPass` already knows
; how to lower into a canonical `feme.cpu.resource.atomic.*` runtime call
; -- see feme/test/Tools/feme-run/HLSL/resource-atomic.hlsl for this raised
; end-to-end through `feme-run`.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { ptr }

; A `RWStructuredBuffer<uint>`-shaped `dx.RawBuffer` UAV at u0, space 0:
; every `AtomicBinOpCode` this pass maps, exercised against the same
; handle/index/value operands -- mirrors `atomic_i32`'s own opcode sweep in
; ResourceAtomicBinOp.ll, the forward direction this inverts.
; CHECK-LABEL: define i32 @atomic_opcodes(
define i32 @atomic_opcodes(i32 %idx, i32 %val) {
  ; CHECK: [[H:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)

  ; CHECK: [[P0:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw add ptr [[P0]], i32 %val
  %add = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 0, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P1:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw and ptr [[P1]], i32 %val
  %and = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 1, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P2:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw or ptr [[P2]], i32 %val
  %or = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 2, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P3:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw xor ptr [[P3]], i32 %val
  %xor = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 3, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P4:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw min ptr [[P4]], i32 %val
  %imin = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 4, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P5:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw max ptr [[P5]], i32 %val
  %imax = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 5, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P6:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw umin ptr [[P6]], i32 %val
  %umin = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 6, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P7:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw umax ptr [[P7]], i32 %val
  %umax = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 7, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK: [[P8:%.*]] = call ptr @llvm.dx.resource.getpointer.{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[H]], i32 %idx)
  ; CHECK: atomicrmw xchg ptr [[P8]], i32 %val
  %xchg = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 8, i32 %idx, i32 poison, i32 poison, i32 %val)
  ; CHECK-NOT: casthandle
  ret i32 %xchg
}

; A genuine `StructuredBuffer`-of-a-struct atomic carries both a struct
; index (Coord0) and an intra-element byte offset (Coord1): raising adds a
; byte-level `getelementptr` off the `getpointer` result for that offset,
; rather than discarding it, before the `atomicrmw` itself.
; CHECK-LABEL: define i32 @atomic_struct_offset(
define i32 @atomic_struct_offset(i32 %idx, i32 %suboffset, i32 %val) {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)
  ; CHECK: [[P:%.*]] = call ptr @llvm.dx.resource.getpointer{{.*}}(target("dx.RawBuffer", i8, 1, 0) %{{.*}}, i32 %idx)
  ; CHECK: [[GEP:%.*]] = getelementptr i8, ptr [[P]], i32 %suboffset
  ; CHECK: atomicrmw add ptr [[GEP]], i32 %val
  %old = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %h, i32 0, i32 %idx, i32 %suboffset, i32 poison, i32 %val)
  ret i32 %old
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32)

!dx.resources = !{!0}

!0 = !{null, !1, null, null}
; UAVs: {ID, GV, name, space, lowerBound, rangeSize, kind, coherent, counter,
;        ROV, props}
!1 = !{!2, !3}
!2 = !{i32 0, ptr undef, !"structured", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!3 = !{i32 1, ptr undef, !"raw", i32 0, i32 1, i32 1, i32 11, i1 false, i1 false, i1 false, !4}
!4 = !{i32 0, i32 9}
