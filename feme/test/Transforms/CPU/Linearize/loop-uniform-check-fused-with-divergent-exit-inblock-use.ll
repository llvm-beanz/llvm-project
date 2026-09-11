; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap H89a/H89b: like `loop-uniform-check-fused-with-divergent-exit.ll`,
; a plain uniform trip-count check's own "exhausted" arm is fused, as one
; incoming value of a `phi`, into another block's own genuinely divergent
; exit check (`check`) -- but here that `phi` is `check`'s *own* condition,
; and its only use besides that in-block terminator is none at all (unlike
; the other test, whose `Flow`'s condition `phi` also feeds a separate
; downstream merge). This is the shape a real
; `dEQP-VK.mesh_shader.ext.properties.max_mesh_output_vertices_256` shader
; hits: `DiamondFlattener` leaves the divergent diamond's own merge `phi`
; (`check`'s condition) completely alone (see `DiamondFlattener::flatten`'s
; uniform-branch case), so `peelConstantFlowPredecessors` (not
; `DiamondFlattener`) is the only thing that ever touches it.
;
; Before this fix, `peelConstantFlowPredecessors`'s `SSAUpdater` call
; rewrote *every* use of `check`'s own condition `phi` -- including its
; use in `check`'s own terminator, still trivially dominated by the `phi`
; itself -- via `SSAUpdater::RewriteUse`. Since that use is not registered
; with an available value for `check`'s *other* real predecessor
; (`divergent.flow`), `SSAUpdater::GetValueInMiddleOfBlock` could not
; resolve it directly and instead synthesized a brand new, semantically
; unrelated `phi` chain reaching all the way back to the loop header,
; permanently corrupting the very exit-check value the masked loop's
; runtime termination depends on -- a latent, silent miscompile that
; would only surface as an infinite loop at runtime (a compile-time
; "divergent branch" rejection was and is the safe fallback that would
; otherwise have caught this shape instead). This fix skips `SSAUpdater`
; entirely for any use still inside `check` itself, since peeling one of
; `check`'s *other* predecessors changes nothing about that in-block use's
; own dominance.

; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK-NEXT: %i = phi i32 [ 0, %entry ], [ %inc, %latch ]
; CHECK: {{^}}check:
; CHECK-NEXT: %cond = phi i1 [ %divergent, %divergent.flow ]
; CHECK-NEXT: %[[NOT:.*]] = xor i1 %cond, true
; CHECK-NEXT: %active.check.live = and i1 %active.live, %[[NOT]]
; CHECK: latch:
; CHECK: feme.cpu.mask.any
define void @main(i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  br label %outercheck
outercheck:
  %outercond = icmp ult i32 %i, %n
  br i1 %outercond, label %innercheck, label %check.crit_edge
check.crit_edge:
  br label %check
innercheck:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %divergent = icmp eq i32 %tid, %i
  br label %divergent.flow
divergent.flow:
  br label %check
check:
  %cond = phi i1 [ %divergent, %divergent.flow ], [ true, %check.crit_edge ]
  br i1 %cond, label %exit, label %latch
latch:
  %inc = add i32 %i, 1
  br label %header
exit:
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
