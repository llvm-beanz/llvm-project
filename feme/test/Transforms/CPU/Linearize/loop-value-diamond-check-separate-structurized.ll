; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; The same "check separate from latch" `for` loop shape as
; `loop-uniform-check-separate-structurized.ll`, but with a genuine
; divergent `if`/`else` inside the loop body whose two arms only merge a
; value (no side effect) -- mirroring a real
; `dEQP-VK.image.load_store_multisample.2d.*` verification shader's own
; `for (sampleNdx...) { ...; if (color == expected) ++checksum; }` loop
; (see roadmap H19g/H19k in feme/docs/Roadmap.md). `feme::cpu::
; foldRedundantFlowBlock` (roadmap H19k) folds away only the loop's own
; redundant exit-check re-derivation (see `loop-uniform-check-separate-
; structurized.ll`'s own comment) -- it is narrowly scoped to a block whose
; branch condition is a `phi` of literal constants, so it does not touch
; this value-only diamond's own, genuinely divergent branch at all.
; `feme::cpu::DiamondFlattener` handles that the same way it always does:
; flattening it into a masked `select` (`live`/`sideeffect` mask bookkeeping
; included, even though this particular diamond has no side effect to
; guard), leaving the loop's own check untouched by any mask machinery
; since it is not itself divergent (`%n` is a uniform argument).

; CHECK-LABEL: define void @main(
; CHECK: br i1 %cond, label %body, label %check.Flow_crit_edge
; CHECK: %divergent = icmp eq i32 %tid, %i
; CHECK: select i1 %divergent, i32 %bumped, i32 %checksum
define void @main(i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %checksum = phi i32 [0, %entry], [%checksum.next, %latch]
  br label %check
check:
  %cond = icmp slt i32 %i, %n
  br i1 %cond, label %body, label %exit
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %divergent = icmp eq i32 %tid, %i
  br i1 %divergent, label %then, label %merge
then:
  %bumped = add i32 %checksum, 1
  br label %merge
merge:
  %checksum.merged = phi i32 [%bumped, %then], [%checksum, %body]
  br label %latch
latch:
  %checksum.next = phi i32 [%checksum.merged, %merge]
  %inc = add i32 %i, 1
  br label %header
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

