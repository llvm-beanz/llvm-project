; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H165: a divergent branch inside a loop body whose arms reconverge
; strictly *inside* the loop body -- at `merge`, not at the loop's own exit
; block (`exit`) or its own latch -- is a mid-body diamond, structurally the
; same shape H158 taught the entry wrapper to outline, one pass earlier.
; Before this fix, `feme::cpu::LoopLinearizer` had no path for this at all:
; it only recognized a divergent branch that relayed into the loop's own
; exit check (`matchExitCheckWithRelay`), so this shape hit the same
; "internal branch... does not reach the loop's exit block" diagnostic as
; `unsupported-loop-internal-branch.ll`'s genuinely-unsupported shape (a
; divergent `continue`, reconverging at the loop's own latch). The
; distinguishing test, `DiamondFlattener::flattenLoopBodyDiamond`, is
; whether the branch's immediate post-dominator validates with an empty
; `CycleBoundaryBlocks` -- i.e. neither arm crosses a loop control edge on
; the way there. Here it does not (both arms reconverge at `merge`, a plain
; interior block distinct from both the header and the latch, which is
; `merge` itself), so the branch is flattened in place with the existing
; select-based diamond mechanism, leaving the loop's own separate, uniform
; exit check (`%loop.cond`) untouched.

; CHECK-LABEL: define void @main(
; CHECK: body:
; CHECK: %tid = call i32 @llvm.dx.thread.id(i32 0)
; CHECK: %cond = icmp eq i32 %tid, 0
; CHECK-NOT: br i1 %cond
; CHECK: %a = add i32 %v, 1
; CHECK: %b = add i32 %v, 2
; CHECK: %v.merge.linearized = select i1 %cond, i32 %a, i32 %b
; CHECK: %inc = add i32 %i, 1
; CHECK: %loop.cond = icmp slt i32 %inc, %n
; CHECK: br i1 %loop.cond, label %loop, label %exit
define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %merge]
  %v = phi i32 [0, %entry], [%v.merge, %merge]
  br label %body
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %cond = icmp eq i32 %tid, 0
  br i1 %cond, label %then, label %else
then:
  %a = add i32 %v, 1
  br label %merge
else:
  %b = add i32 %v, 2
  br label %merge
merge:
  %v.merge = phi i32 [%a, %then], [%b, %else]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
