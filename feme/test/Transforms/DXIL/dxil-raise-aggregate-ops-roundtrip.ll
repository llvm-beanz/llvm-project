; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops -S | FileCheck %s

; End-to-end validation that feme::dxil::OpRaisingPass's aggregate-returning
; mechanism (raiseAggregateCall in OpRaising.cpp) is a genuine inverse of
; LLVM's own `DXILOpLowering` pass, not just of hand-written `dx.op.*` IR
; matching this pass's own assumptions -- mirrors dxil-raise-ops-roundtrip.ll,
; but for `IMul`/`UAddc`/`SplitDouble`/`WaveActiveBallot` (see roadmap step R3
; in feme/docs/Roadmap.md).

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.7-compute"

; CHECK-LABEL: define i32 @imul(
define i32 @imul(i32 %a, i32 %b) {
  ; CHECK: %[[R:.*]] = call { i32, i32 } @llvm.dx.imul.i32(i32 %a, i32 %b)
  %r = call {i32, i32} @llvm.dx.imul.i32(i32 %a, i32 %b)
  ; CHECK: %[[HI:.*]] = extractvalue { i32, i32 } %[[R]], 0
  ; CHECK: %[[LO:.*]] = extractvalue { i32, i32 } %[[R]], 1
  %hi = extractvalue {i32,i32} %r, 0
  %lo = extractvalue {i32,i32} %r, 1
  ; CHECK: add i32 %[[HI]], %[[LO]]
  %sum = add i32 %hi, %lo
  ret i32 %sum
}

; CHECK-LABEL: define i32 @uaddc(
define i32 @uaddc(i32 %a, i32 %b) {
  ; CHECK: %[[R:.*]] = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  %r = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  ; CHECK: %[[SUM:.*]] = extractvalue { i32, i1 } %[[R]], 0
  ; CHECK: %[[CARRY:.*]] = extractvalue { i32, i1 } %[[R]], 1
  %sum = extractvalue {i32,i1} %r, 0
  %carry = extractvalue {i32,i1} %r, 1
  %carry32 = zext i1 %carry to i32
  ; CHECK: add i32 %[[SUM]],
  %res = add i32 %sum, %carry32
  ret i32 %res
}

; CHECK-LABEL: define i32 @splitdouble(
define i32 @splitdouble(double %a) {
  ; CHECK: %[[R:.*]] = call { i32, i32 } @llvm.dx.splitdouble.i32(double %a)
  %r = call {i32, i32} @llvm.dx.splitdouble.i32(double %a)
  ; CHECK: %[[LO:.*]] = extractvalue { i32, i32 } %[[R]], 0
  ; CHECK: %[[HI:.*]] = extractvalue { i32, i32 } %[[R]], 1
  %lo = extractvalue {i32,i32} %r, 0
  %hi = extractvalue {i32,i32} %r, 1
  ; CHECK: add i32 %[[LO]], %[[HI]]
  %res = add i32 %lo, %hi
  ret i32 %res
}

; CHECK-LABEL: define i32 @ballot(
define i32 @ballot(i1 %a) {
  ; CHECK: %[[R:.*]] = call { i32, i32, i32, i32 } @llvm.dx.wave.ballot.i32(i1 %a)
  %r = call {i32,i32,i32,i32} @llvm.dx.wave.ballot.i32(i1 %a)
  ; CHECK: %[[X:.*]] = extractvalue { i32, i32, i32, i32 } %[[R]], 0
  ; CHECK: %[[Y:.*]] = extractvalue { i32, i32, i32, i32 } %[[R]], 1
  %x = extractvalue {i32,i32,i32,i32} %r, 0
  %y = extractvalue {i32,i32,i32,i32} %r, 1
  ; CHECK: %[[CNT:.*]] = call i32 @llvm.ctpop.i32(i32 %[[X]])
  %cnt = call i32 @llvm.ctpop.i32(i32 %x)
  ; CHECK: add i32 %[[CNT]], %[[Y]]
  %res = add i32 %cnt, %y
  ret i32 %res
}

declare {i32, i32} @llvm.dx.imul.i32(i32, i32)
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32)
declare {i32, i32} @llvm.dx.splitdouble.i32(double)
declare {i32,i32,i32,i32} @llvm.dx.wave.ballot.i32(i1)
declare i32 @llvm.ctpop.i32(i32)
