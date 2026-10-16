; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap L419(h): a loop whose header holds the real (uniform) trip-count
; check, with a *separate*, later block in the body holding its own
; divergent `break` check that branches straight to the loop's own exit
; block -- distinct from `loop-break.ll`'s supported shape, where the
; divergent check is fused directly into the header itself (so
; `LoopLinearizer::linearizeCycle`'s `HeaderDivergent`/`stayInLoop` path
; handles it) and `loop-break-structurized.ll`'s supported shape, where the
; break's own reconvergence block is a pure relay the loop's real exit
; check sits just past (so `DiamondFlattener::validate`'s
; `isLoopControlOrHeaderRelayEdge` chase recognizes it).
;
; This exact shape -- a body block's own branch reaching the *current*
; cycle's real exit block directly, with no relay chain to chase through --
; is already supported when the loop body has no mask-affecting operation
; of its own (see `loop-break-separate-from-header-check.ll`, which is
; identical but for the `feme.cpu.resource.store.raw.i32` call below): it
; goes through `LoopLinearizer::linearizeCycle`'s simpler, non-masked
; `collectUniformPassThroughRegion`/`exited.via.check` path, which already
; tracks a body-level break correctly. The moment the body also contains a
; mask-affecting call (here, an ordinary resource store, any of the
; `StageOpKind`s `CycleHasMaskOps` also recognizes would trigger the same
; gap), `linearizeCycle` instead takes its "fully uniform loop with body
; mask ops" branch, built on `DiamondFlattener::canFlattenLoopBodyRegion`/
; `flattenLoopBodyRegion` over the `[HeaderStayInLoop, Latch)` region --
; and that path's own `validate` walk classifies the body block's branch to
; the loop's exit as a cycle-boundary edge (`isLoopControlEdge`'s
; `!CI.contains(C, Target)` test, true for the cycle's own real exit block
; exactly as it is for a different, nested child cycle's own control edge)
; and defers it as "LoopLinearizer's problem, not an error" -- except this
; call *is* `LoopLinearizer`, already in the middle of processing this
; exact cycle, with no other call left to defer to. `canFlattenLoopBodyRegion`
; then fails because `CycleBoundaryBlocks` is non-empty, and the body's own
; break is left entirely unhandled.
;
; This reduces `dEQP-VK.glsl.loops.special.while_uniform_iterations.
; conditional_break_{fragment,vertex}` (and the identically-shaped
; `for_uniform_iterations.conditional_break_*`/`do_while_uniform_iterations.
; conditional_break_*`/`for_dynamic_iterations.{if,else}block_*`/
; `while_dynamic_iterations.{if,else}block_*`/`for_uniform_iterations.
; mixed_break_continue_*`/`while_uniform_iterations.mixed_break_continue_*`
; -- 18 cases total, this session's own CTS rerun of `dEQP-VK.glsl.loops.
; special.*`) down to its essential structure. Not yet fixed: generalizing
; the masked-loop path to recognize and correctly mask this shape (folding
; a body-level break into the same kind of sticky, loop-carried "this lane
; already decided not to continue" state `HeaderDivergent`'s own
; `stayInLoop` already gives the header's *own* check, generalized to
; however many separate break points a body can have, each needing its own
; contribution OR'd together and its own exit-block phi values preserved)
; is a materially larger lift than this one session's remaining time
; allowed -- see roadmap `L419(h)`'s own updated entry.

; CHECK: feme-cpu-linearize: function 'main': loop at 'header' has an internal branch in a body shape this milestone does not yet lower
define void @main(ptr %heap, i32 %heap_count, i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %body, label %exit
body:
  call void @feme.cpu.resource.store.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i32 1, i1 true)
  %break.cond = icmp eq i32 %inc, %n
  br i1 %break.cond, label %exit, label %latch
latch:
  br label %header
exit:
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
