; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; (Roadmap L85) `Intrinsic::spv_subgroup_ballot`'s own `<4 x i32>` result
; is control-flow-*dependent* (its predicate operand `%pred` is itself
; divergent, since it compares a per-lane thread id) but not
; control-flow-*varying*: `subgroupBallot()`'s own definition guarantees
; every lane's copy of the result is the identical whole-subgroup mask.
; This test isolates `classifyWaveCall`'s SPIR-V-origin `Ballot`
; classification and `widenWaveCall`'s struct-to-vector repackaging
; (`feme.cpu.wave.ballot.v4`'s own `{i32,i32,i32,i32}` DXIL-shaped result
; is unpacked back into the `<4 x i32>` vector shape this intrinsic's
; own callers expect) without exercising `feme-cpu-lower-wave`'s own
; separate DXIL-focused `lowerBallot` (see `wave-lowering-ballot.ll`).

; CHECK-LABEL: define void @main(
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id.v4(
; CHECK: %[[PRED:.*]] = icmp eq <4 x i32> %[[TID]], zeroinitializer
; CHECK: %[[CALL:.*]] = call { i32, i32, i32, i32 } @feme.cpu.wave.ballot.v4(<4 x i1> %wave_entry_mask, <4 x i1> %[[PRED]])
; CHECK: %[[W0:.*]] = extractvalue { i32, i32, i32, i32 } %[[CALL]], 0
; CHECK: %[[V0:.*]] = insertelement <4 x i32> poison, i32 %[[W0]], i32 0
; CHECK: %[[W1:.*]] = extractvalue { i32, i32, i32, i32 } %[[CALL]], 1
; CHECK: %[[V1:.*]] = insertelement <4 x i32> %[[V0]], i32 %[[W1]], i32 1
; CHECK: %[[W2:.*]] = extractvalue { i32, i32, i32, i32 } %[[CALL]], 2
; CHECK: %[[V2:.*]] = insertelement <4 x i32> %[[V1]], i32 %[[W2]], i32 2
; CHECK: %[[W3:.*]] = extractvalue { i32, i32, i32, i32 } %[[CALL]], 3
; CHECK: insertelement <4 x i32> %[[V2]], i32 %[[W3]], i32 3
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %pred = icmp eq i32 %tid, 0
  %r = call <4 x i32> @llvm.spv.subgroup.ballot(i1 %pred)
  %x = extractelement <4 x i32> %r, i32 0
  %cnt = call i32 @llvm.ctpop.i32(i32 %x)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <4 x i32> @llvm.spv.subgroup.ballot(i1)
declare i32 @llvm.ctpop.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
