; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L419(h): companion to `unsupported-loop-internal-break-with-mask-op.ll`
; (see its own, much longer comment for the full context) -- the identical
; shape, minus that test's one `feme.cpu.resource.store.raw.i32` call, so
; this loop has no mask-affecting operation anywhere in its body.
; `LoopLinearizer::linearizeCycle` takes its simpler, non-masked
; `collectUniformPassThroughRegion`/`exited.via.check` path for a loop like
; this, which already supports a body-level `break` found downstream of the
; header's own separate (uniform) trip-count check -- unlike the sibling
; test's "fully uniform loop with body mask ops" path (built on
; `DiamondFlattener::canFlattenLoopBodyRegion`/`flattenLoopBodyRegion`),
; which does not yet recognize this same shape. Pinning this success case
; here confirms the sibling test's own failure is specific to that one
; code path, not to the break-downstream-of-header shape in general.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %exited.via.check = phi i1 [ false, %entry ], [ %exited.via.check.next, %latch ]
; CHECK: body:
; CHECK: %break.cond = icmp eq i32 %tid, %inc
define void @main(i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %body, label %exit
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %inc
  br i1 %break.cond, label %exit, label %latch
latch:
  br label %header
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
