; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; (Roadmap L85) A `WaveActiveBallot`/`subgroupBallot` call's own predicate
; operand (operand 0) gets ANDed with the block's live mask when it sits
; under a divergent branch's arm, exactly like a canonical
; `feme.cpu.resource.*` call's own mask operand does (see
; `resource-call-masked.ll`): its result is a ballot over "the group's
; currently active invocations", which is no longer implicit in which arm
; of a real branch reached it once this pass flattens that branch away.
; Missing this let a `subgroupBallot(true)` inside a divergent arm see
; every wave-active lane instead of just the lanes that actually reached
; that arm -- found reducing a real
; `dEQP-VK.subgroups.ballot_other.compute.subgroupballotfindlsb` failure
; down to this exact shape.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c
; CHECK: t:
; CHECK: %ballot.pred.masked = and i1 true, %live.t
; CHECK: call <4 x i32> @llvm.spv.subgroup.ballot(i1 %ballot.pred.masked)
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %r = call <4 x i32> @llvm.spv.subgroup.ballot(i1 true)
  %x = extractelement <4 x i32> %r, i32 0
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <4 x i32> @llvm.spv.subgroup.ballot(i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
