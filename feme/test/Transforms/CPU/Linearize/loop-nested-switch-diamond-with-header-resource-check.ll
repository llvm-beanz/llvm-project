; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L419(ad): a nested, `switch`-lowered multi-diamond loop body
; (`NodeBlock`/`Flow3`/`Flow4` below, the shape `structurizeCFG` produces
; for a `switch` or chained `if`/`else if`/`else` inside a loop with mixed
; `break`/`continue` paths) whose loop `Header` (block `0` below) holds its
; own, separate, uniform-looking trip-count check that reads a resource
; load.
;
; Root-caused as the **same underlying gap `L419(y)` fixed, not a separate
; `DiamondFlattener::validate` bug as a prior session's investigation
; first concluded**: that investigation found `validate`'s ordinary-diamond
; model picks its reconvergence point via `R = immediatePostDom(Cur)`, and
; diagnosed that when `Header`'s own resource-load-derived trip check is
; *also* swept into the same region `validate` walks (because
; `UniformityInfo`'s pre-masking snapshot judges it uniform, so nothing
; excludes it), `R` can escape all the way to the function's own `ret`
; block (a true post-dominator of everything, but far outside both the
; cycle and the region actually under validation), producing the generic
; "internal branch in a body shape this milestone does not yet lower"
; diagnostic. **That diagnosis was correct, but the identified trigger --
; `Header`'s own check being wrongly judged uniform -- is exactly
; `L419(y)`'s own root cause, not a second, independent one.** Confirmed by
; temporarily reverting `L419(y)`'s fix (`dependsOnResourceOrImageLoadInCycle`
; folded into `HeaderDivergent`): this exact repro reproduces the identical
; diagnostic with the fix reverted, and compiles clean with it restored --
; no `L419(ad)`-specific code change was needed. Once `HeaderDivergent` is
; correctly predicted `true` ahead of time (the resource load feeding
; `Header`'s own trip check forces `cycleHasMaskOps(C)` regardless of
; `Header`'s own divergence, exactly as `L419(y)`'s fix already handles),
; the existing `if (HeaderDivergent)` recovery advances `PreRegionStart`
; past `Header` *before* the switch-lowered body's own region is ever
; collected or validated -- so the body's own, self-contained diamond
; nest no longer has `Header`'s own branch (and its true reconvergence
; point outside the region) folded into the same `validate` walk at all,
; and the pre-existing post-dominator escape this row diagnosed simply
; never arises. Fixes `dEQP-VK.glsl.loops.special.*.mixed_break_continue_
; {fragment,vertex}` (18 cases, every `for`/`while`/`while_constant`/
; `while_dynamic`/`while_uniform` x `{fragment,vertex}` combination, a
; strictly larger set than this row's originally-tracked 4
; `for_uniform_iterations` cases -- the other 14 combinations were
; already passing independently of this gap).
; CHECK-LABEL: define void @main(
; CHECK: %active.live = phi i1 [ true, %entry ], [ %active.header.live,
; CHECK: %1 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 %active.live)
; CHECK: %2 = icmp slt i32 %.0, %1
; CHECK: %active.header.live = and i1 %active.live, %2
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header.live)
; CHECK: NodeBlock:
; CHECK: %Pivot = icmp sge i32 %.0, 3
; CHECK: Flow4:
; CHECK: %6 = phi i1
; CHECK: %7 = phi i1
define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
entry:
  br label %9

9:                                                ; preds = %Flow._crit_edge, %entry
  %.0 = phi i32 [ 0, %entry ], [ %26, %Flow._crit_edge ]
  %10 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %11 = icmp slt i32 %.0, %10
  br i1 %11, label %25, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %9
  br label %Flow

Flow6:                                            ; preds = %Flow5.Flow6_crit_edge, %23
  %12 = phi i32 [ %24, %23 ], [ poison, %Flow5.Flow6_crit_edge ]
  %14 = phi i1 [ false, %23 ], [ true, %Flow5.Flow6_crit_edge ]
  br label %Flow

exit:                                             ; preds = %Flow
  ret void

Flow5:                                            ; preds = %Flow4.Flow5_crit_edge, %33
  %22 = phi i1 [ true, %33 ], [ %31, %Flow4.Flow5_crit_edge ]
  br i1 %22, label %23, label %Flow5.Flow6_crit_edge

Flow5.Flow6_crit_edge:                            ; preds = %Flow5
  br label %Flow6

23:                                               ; preds = %Flow5
  %24 = add i32 %.0, 1
  br label %Flow6

25:                                               ; preds = %9
  br label %NodeBlock

Flow:                                             ; preds = %.Flow_crit_edge, %Flow6
  %26 = phi i32 [ %12, %Flow6 ], [ poison, %.Flow_crit_edge ]
  %28 = phi i1 [ %14, %Flow6 ], [ true, %.Flow_crit_edge ]
  br i1 %28, label %exit, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %9

NodeBlock:                                        ; preds = %25
  %Pivot = icmp sge i32 %.0, 3
  br i1 %Pivot, label %LeafBlock1, label %NodeBlock.Flow3_crit_edge

NodeBlock.Flow3_crit_edge:                        ; preds = %NodeBlock
  br label %Flow3

LeafBlock1:                                       ; preds = %NodeBlock
  %SwitchLeaf2 = icmp ne i32 %.0, 3
  br label %Flow3

Flow3:                                            ; preds = %NodeBlock.Flow3_crit_edge, %LeafBlock1
  %29 = phi i1 [ %SwitchLeaf2, %LeafBlock1 ], [ false, %NodeBlock.Flow3_crit_edge ]
  %30 = phi i1 [ false, %LeafBlock1 ], [ true, %NodeBlock.Flow3_crit_edge ]
  br i1 %30, label %LeafBlock, label %Flow3.Flow4_crit_edge

Flow3.Flow4_crit_edge:                            ; preds = %Flow3
  br label %Flow4

LeafBlock:                                        ; preds = %Flow3
  %SwitchLeaf = icmp ne i32 %.0, 0
  br label %Flow4

Flow4:                                            ; preds = %Flow3.Flow4_crit_edge, %LeafBlock
  %31 = phi i1 [ true, %LeafBlock ], [ false, %Flow3.Flow4_crit_edge ]
  %32 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %29, %Flow3.Flow4_crit_edge ]
  br i1 %32, label %33, label %Flow4.Flow5_crit_edge

Flow4.Flow5_crit_edge:                            ; preds = %Flow4
  br label %Flow5

33:                                               ; preds = %Flow4
  br label %Flow5
}

declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
