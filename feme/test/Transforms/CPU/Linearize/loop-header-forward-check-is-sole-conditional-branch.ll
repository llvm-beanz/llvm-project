; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L462: `Header` is a genuinely trivial, single-successor
; pass-through block (only the induction `phi`, no `CondBr` of its own),
; with its real, uniform-looking trip-count check one hop forward in
; `check` -- exactly the shape `findForwardExitCheckBlock`'s own
; "Roadmap L432(a)" fallback exists for. `check`'s condition reads a
; resource load whose mask operand is still the literal, compile-time
; constant `i1 true` `UniformityInfo`'s one-time, pre-masking snapshot
; sees (see "Roadmap L419(y)"'s own comment), so `isDivergentBranch`
; judges it uniform at the point the forward-match guard
; ("Roadmap L432(c)") is evaluated, and the guard let it through as
; `HeaderExit`. `check` is also this cycle's *only* `OtherCondBrBlocks`
; member, so the "Roadmap L419(h)" widening (gated on
; `cycleHasMaskOps(C) && OtherCondBrBlocks.size() == 1`) independently
; recognized the very same block as `CheckBlock` too -- both code paths
; then erased and replaced the same, already-detached `CondBr`,
; asserting ("Instruction to insert before is not in a basic block!")
; inside `linearizeCycle`'s `CheckExit` handling. Reduced from the real
; crash in `dEQP-VK.spirv_assembly.instruction.spirv1p4.loop_control.
; {iteration_multiple,max_iterations,min_iterations,partial_count,
; peel_count}` (glslang's own lowering of a `for` loop reading a
; dynamic, uniform trip count from a resource puts the loop header and
; its first real check in separate blocks exactly like this, after
; `StructurizeCFG` removes the critical edge between them).
;
; Fixed by reusing the same, more accurate
; `dependsOnResourceOrImageLoadInCycle` test "Roadmap L419(y)" already
; uses to correct `HeaderDivergent`/`LatchDivergent` after the fact:
; the forward-match guard now also declines a forward check whose
; condition depends on any resource/image load inside this cycle, not
; just one `UniformityInfo` already judges divergent -- so `check`
; remains unclaimed by `HeaderExit` and is instead handled exactly once,
; by the `CheckBlock` machinery that already knows how to freeze/select/
; narrow-mask it correctly.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.check.live, %latch ]
; CHECK: br label %check
; CHECK: check:
; CHECK: %val = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 %active.live)
; CHECK: %cond = icmp slt i32 %i, %val
; CHECK: %active.check.live = and i1 %active.live, %cond
; CHECK: br label %body
; CHECK: body:
; CHECK: latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK: br i1 %loop.any.active, label %header, label %exit
define void @main(ptr %heap, i32 %heap_count) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  br label %check
check:
  %val = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 true)
  %cond = icmp slt i32 %i, %val
  br i1 %cond, label %body, label %exit
body:
  %inc = add i32 %i, 1
  br label %latch
latch:
  br label %header
exit:
  ret void
}
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
