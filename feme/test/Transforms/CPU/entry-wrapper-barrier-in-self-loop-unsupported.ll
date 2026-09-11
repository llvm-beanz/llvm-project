; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap H72 (feme/docs/Roadmap.md): a self-loop whose own header block
; contains a `..._with_group_sync` barrier is NOT the shape
; `matchBarrierFreeLoop` recognizes -- unlike
; entry-wrapper-barrier-free-loop-self.ll's spin-wait, splitting this
; barrier would need a region split *per loop iteration*, which this
; milestone's flat, whole-region `outlineChain` has no way to represent
; (same reasoning as `entry-wrapper-barrier-in-branch-merge-phi-
; unsupported.ll`'s arm case). `matchBarrierFreeLoop` must check the
; self-loop header itself for a barrier, not just the (here, empty)
; chain of blocks between the header and its own backedge, or it would
; wrongly accept this shape and later trip an internal consistency
; assertion in `splitAtGroupSyncBarriers`.

; CHECK: error: feme-cpu-wrap-entry: function 'main' has a barrier inside non-linear control flow
define void @main() #0 {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %header ]
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  %cond = icmp ult i32 %i.next, 4
  br i1 %cond, label %header, label %after

after:
  ret void
}
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
