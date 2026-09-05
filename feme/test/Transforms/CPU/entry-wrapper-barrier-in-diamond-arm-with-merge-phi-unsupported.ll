; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap L45: a genuinely unsafe diamond -- one whose arm itself contains
; a `..._with_group_sync` barrier, *and* whose merge block has a phi -- is
; still diagnosed. `feme::cpu::matchBranchShape` declines it (a merge phi
; means threading a value across the wrapper's own scalar branch choice,
; see entry-wrapper-barrier-in-branch-merge-phi-unsupported.ll), and
; `isLinearChain`'s own new "safe diamond" case (see
; entry-wrapper-safe-diamond-after-barrier.ll) declines it too, since the
; arm containing the barrier is not itself barrier-free
; (`walkBarrierFreeArm`) -- so region splitting correctly falls back to
; the pre-existing diagnostic instead of (unsoundly) treating this as the
; safe shape.

; CHECK: error: feme-cpu-wrap-entry: function 'main' has a barrier inside non-linear control flow
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %cond = icmp eq i32 %gid, 0
  br i1 %cond, label %a, label %b
a:
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %vala = add i32 %gid, 10
  br label %exit
b:
  br label %exit
exit:
  %val = phi i32 [ %vala, %a ], [ 0, %b ]
  %doubled = mul i32 %val, 2
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
