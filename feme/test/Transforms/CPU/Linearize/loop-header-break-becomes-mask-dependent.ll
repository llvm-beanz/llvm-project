; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L419(y): `Header`'s own exit check (`%break.cond` below) reads a
; resource load (`%val`) whose mask operand is still the literal,
; compile-time constant `i1 true` `UniformityInfo` sees at the point
; `HeaderDivergent` is first computed -- genuinely uniform by that
; snapshot's own lights, since nothing has narrowed it yet. `body`'s own,
; separate, uniform trip-count check (`%trip.done`, also resource-load
; derived) merges with `Header`'s own break decision at `flow`, which
; `LoopLinearizer::linearizeCycle`'s pre-existing `L419(h)` widening
; (`RealExitCandidates.empty() && cycleHasMaskOps(C) &&
; OtherCondBrBlocks.size() == 1`) recognizes as this cycle's own
; `CheckBlock` via `matchExitCheckWithRelay`.
;
; Previously, since `HeaderDivergent` was `false`, `PreRegionStart` stayed
; `Header` itself (rather than being advanced past it the way the
; `if (HeaderDivergent)` recovery already does for a check
; `UniformityInfo` itself judged divergent), so `collectUniformPassThroughRegion`
; walked straight through `Header`'s own branch as an ordinary,
; tolerated, uniform region-interior `CondBr` -- only for
; `applyStageMasks` to then narrow `%val`'s own mask operand to
; `%active.live` once it masked `Header`'s block as part of that same
; region, and `flattenTaintedBranchesInRegion`'s later re-scan to find
; `Header`'s own branch newly mask-dependent and route it to
; `DiamondFlattener::flattenKnownDivergentBranch`, which correctly
; refused it: `Header`'s own true reconvergence point is this cycle's own
; exit machinery (`flow`/`latch`), not a local diamond
; `DiamondFlattener` can fold. This produced the diagnosed "loop has a
; branch in 'header' that became mask-dependent after masking but does
; not have the simple shape this pass can flatten" failure, reducing
; `dEQP-VK.glsl.loops.special.{do_while,for,while}_uniform_iterations.
; conditional_break_{fragment,vertex}` (6 cases).
;
; Fixed by `dependsOnResourceOrImageLoadInCycle`: whenever this cycle is
; already known to need real masking for some other reason independent of
; `Header`'s/`Latch`'s own divergence (here, `cycleHasMaskOps(C)` is true
; purely because resource loads exist in the cycle's body at all --
; `Header`'s own `%val` load counts towards that by itself), and
; `Header`'s/`Latch`'s own exit check depends on *any* resource/image
; load located inside this same cycle (regardless of whether that load's
; mask operand already looks non-constant), predict `HeaderDivergent`/
; `LatchDivergent` true ahead of time -- correctly routing `Header`'s own
; check through the exact same freeze/select/narrow-mask machinery the
; `if (HeaderDivergent)` recovery already implements for the
; already-known-divergent case, rather than leaving it to
; `flattenTaintedBranchesInRegion`'s own after-the-fact, local-diamond-only
; patch-up.
; CHECK-LABEL: define void @main(
; CHECK: header:
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.check.live, %latch ]
; CHECK: %val = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 %active.live)
; CHECK: %break.cond = icmp eq i32 %inc, %val
; CHECK: br label %body
; CHECK: body:
; CHECK: %limit = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 1, i64 0, i1 %active.header.check.live)
; CHECK: %trip.done = icmp sge i32 %inc, %limit
; CHECK: br label %flow
; CHECK: flow:
; CHECK: %active.check.live = and i1 %active.header.check.live,
; CHECK: br label %latch
; CHECK: latch:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK: br i1 %loop.any.active, label %header, label %exit
define void @main(ptr %heap, i32 %heap_count) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %inc = add i32 %i, 1
  %val = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 0, i64 0, i1 true)
  %break.cond = icmp eq i32 %inc, %val
  br i1 %break.cond, label %flow.from.header, label %body
flow.from.header:
  br label %flow
body:
  %limit = call i32 @feme.cpu.resource.load.raw.i32(ptr %heap, i32 %heap_count, i32 1, i64 0, i1 true)
  %trip.done = icmp sge i32 %inc, %limit
  br label %flow
flow:
  %exit.now = phi i1 [true, %flow.from.header], [%trip.done, %body]
  br i1 %exit.now, label %exit, label %latch
latch:
  br label %header
exit:
  ret void
}
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
