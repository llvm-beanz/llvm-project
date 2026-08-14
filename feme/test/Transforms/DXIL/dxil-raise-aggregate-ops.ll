; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's general multi-return-value raising
; mechanism (raiseAggregateCall/RaisableAggregateOp in OpRaising.cpp):
; `IMul`/`UMul`/`UAddc`/`SplitDouble`/`WaveActiveBallot` all return a DXIL
; aggregate (`%dx.types.twoi32`/`%dx.types.i32c`/`%dx.types.splitdouble`/
; `%dx.types.fouri32`) that must be rewritten to the equivalent LLVM
; intrinsic call's own (differently-typed, but layout-identical) anonymous
; struct, with each `extractvalue` of the old call rewritten to read the new
; one instead -- these opcodes were deferred together precisely because they
; all need this same mechanism (see the DXIL section of feme/docs/Design.md
; and roadmap step R3 in feme/docs/Roadmap.md). This test hand-writes
; already-lowered `dx.op.*` IR (the shape `DXILOpLowering` produces);
; dxil-raise-aggregate-ops-roundtrip.ll separately validates this against
; real `-dxil-op-lower` output.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.7-compute"

%dx.types.twoi32 = type { i32, i32 }
%dx.types.i32c = type { i32, i1 }
%dx.types.splitdouble = type { i32, i32 }
%dx.types.fouri32 = type { i32, i32, i32, i32 }


; CHECK-LABEL: define i32 @imul(
define i32 @imul(i32 %a, i32 %b) {
  ; CHECK: %[[R:.*]] = call { i32, i32 } @llvm.dx.imul.i32(i32 %a, i32 %b)
  ; CHECK: %[[HI:.*]] = extractvalue { i32, i32 } %[[R]], 0
  ; CHECK: %[[LO:.*]] = extractvalue { i32, i32 } %[[R]], 1
  %r = call %dx.types.twoi32 @dx.op.binaryWithTwoOuts.i32(i32 41, i32 %a, i32 %b)
  %hi = extractvalue %dx.types.twoi32 %r, 0
  %lo = extractvalue %dx.types.twoi32 %r, 1
  ; CHECK: %sum = add i32 %[[HI]], %[[LO]]
  %sum = add i32 %hi, %lo
  ret i32 %sum
}

; CHECK-LABEL: define i32 @umul(
define i32 @umul(i32 %a, i32 %b) {
  ; CHECK: %[[R:.*]] = call { i32, i32 } @llvm.dx.umul.i32(i32 %a, i32 %b)
  ; CHECK: %[[HI:.*]] = extractvalue { i32, i32 } %[[R]], 0
  ; CHECK: %[[LO:.*]] = extractvalue { i32, i32 } %[[R]], 1
  %r = call %dx.types.twoi32 @dx.op.binaryWithTwoOuts.i32(i32 42, i32 %a, i32 %b)
  %hi = extractvalue %dx.types.twoi32 %r, 0
  %lo = extractvalue %dx.types.twoi32 %r, 1
  ; CHECK: %sum = add i32 %[[HI]], %[[LO]]
  %sum = add i32 %hi, %lo
  ret i32 %sum
}

; CHECK-LABEL: define i32 @uaddc(
define i32 @uaddc(i32 %a, i32 %b) {
  ; CHECK: %[[R:.*]] = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  ; CHECK: %[[SUM:.*]] = extractvalue { i32, i1 } %[[R]], 0
  ; CHECK: %[[CARRY:.*]] = extractvalue { i32, i1 } %[[R]], 1
  %r = call %dx.types.i32c @dx.op.binaryWithCarryOrBorrow.i32(i32 44, i32 %a, i32 %b)
  %sum = extractvalue %dx.types.i32c %r, 0
  %carry = extractvalue %dx.types.i32c %r, 1
  ; CHECK: %carry32 = zext i1 %[[CARRY]] to i32
  %carry32 = zext i1 %carry to i32
  ; CHECK: %res = add i32 %[[SUM]], %carry32
  %res = add i32 %sum, %carry32
  ret i32 %res
}

; CHECK-LABEL: define i32 @splitdouble(
define i32 @splitdouble(double %a) {
  ; CHECK: %[[R:.*]] = call { i32, i32 } @llvm.dx.splitdouble.i32(double %a)
  ; CHECK: %[[LO:.*]] = extractvalue { i32, i32 } %[[R]], 0
  ; CHECK: %[[HI:.*]] = extractvalue { i32, i32 } %[[R]], 1
  %r = call %dx.types.splitdouble @dx.op.splitDouble.f64(i32 102, double %a)
  %lo = extractvalue %dx.types.splitdouble %r, 0
  %hi = extractvalue %dx.types.splitdouble %r, 1
  ; CHECK: %res = add i32 %[[LO]], %[[HI]]
  %res = add i32 %lo, %hi
  ret i32 %res
}

; CHECK-LABEL: define i32 @wave_active_ballot(
define i32 @wave_active_ballot(i1 %a) {
  ; CHECK: %[[R:.*]] = call { i32, i32, i32, i32 } @llvm.dx.wave.ballot.i32(i1 %a)
  ; CHECK: %[[X:.*]] = extractvalue { i32, i32, i32, i32 } %[[R]], 0
  ; CHECK: %[[Y:.*]] = extractvalue { i32, i32, i32, i32 } %[[R]], 1
  %r = call %dx.types.fouri32 @dx.op.waveActiveBallot(i32 116, i1 %a)
  %x = extractvalue %dx.types.fouri32 %r, 0
  %y = extractvalue %dx.types.fouri32 %r, 1
  ; CHECK: %[[CNT:.*]] = call i32 @llvm.ctpop.i32(i32 %[[X]])
  %cnt = call i32 @dx.op.unaryBits.i32(i32 31, i32 %x)
  ; CHECK: %res = add i32 %[[CNT]], %[[Y]]
  %res = add i32 %cnt, %y
  ret i32 %res
}

; A `dx.op.*` call this pass raises whose result is used any way other than
; a single-index `extractvalue` (here, passed on to an unrelated call) must
; be left as an unmodified `dx.op.*` call rather than erroring, matching
; every other raiser's treatment of an unrecognized shape.
; CHECK-LABEL: define void @unsupported_use(
define void @unsupported_use(i32 %a, i32 %b) {
  ; CHECK: call %dx.types.twoi32 @dx.op.binaryWithTwoOuts.i32(i32 41, i32 %a, i32 %b)
  %r = call %dx.types.twoi32 @dx.op.binaryWithTwoOuts.i32(i32 41, i32 %a, i32 %b)
  call void @sink(%dx.types.twoi32 %r)
  ret void
}

declare %dx.types.twoi32 @dx.op.binaryWithTwoOuts.i32(i32, i32, i32)
declare %dx.types.i32c @dx.op.binaryWithCarryOrBorrow.i32(i32, i32, i32)
declare %dx.types.splitdouble @dx.op.splitDouble.f64(i32, double)
declare %dx.types.fouri32 @dx.op.waveActiveBallot(i32, i1)
declare i32 @dx.op.unaryBits.i32(i32, i32)
declare void @sink(%dx.types.twoi32)
