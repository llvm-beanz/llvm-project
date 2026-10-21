; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; Roadmap ENV5(c)(2): a loop-body switch-decode diamond chain
; (`NodeBlock5`/`NodeBlock`/`Flow`/`LeafBlock`/`LeafBlock3`/`Flow7`/
; `Flow8`/`Flow9`/`Flow10`/`Flow11`) inside a hull-stage
; `main.patchconstant` entry point used to trip
; `foldRedundantFlowBlock`'s own value-forwarding loop into silently
; aliasing two genuinely distinct incoming values into one merge-phi
; slot. `foldRedundantFlowBlocksInCycle` applies `foldRedundantFlowBlock`
; repeatedly to a fixed point within the same cycle: `Flow`'s own
; condition phi is fully constant, so it folds first, forwarding its
; value-phi into `Flow10`'s existing `Flow.Flow10_crit_edge` phi slot and
; retargeting `Flow9`'s terminator to branch straight into that same
; relay. That retargeting is what then exposes the bug: `Flow7`'s own
; condition phi is *also* fully constant, so the same fixed-point loop
; folds `Flow7` next -- but `Flow7`'s own two immediate targets (one
; through `22`/`Flow9`, the other through `Flow7.Flow9_crit_edge`) now
; both reconverge at that very same `Flow9`-then-`Flow.Flow10_crit_edge`
; relay chain before reaching any other merge point, because the
; earlier `Flow` fold already collapsed the path between them. The two
; predecessors `foldRedundantFlowBlock` is about to fold for `Flow7`
; therefore resolve to the exact same `Merges[I]`/`IncomingBlocks[I]`
; pair, and its forwarding loop's single `MergePN->setIncomingValue`
; write for that shared slot runs twice, with the second write silently
; clobbering the first and discarding one of the two genuinely-different
; forwarded values outright -- producing an "Instruction does not
; dominate all uses!" verifier failure when the discarded value is later
; read back out of that slot by something that only the discarded path
; actually dominates. Fixed by checking, right after
; `Merges[I]`/`IncomingBlocks[I]` are computed for both of `BB`'s
; targets, whether they have already coincided (`Merges[0] ==
; Merges[1] && IncomingBlocks[0] == IncomingBlocks[1]`) and bailing out
; of the fold for `BB` in that case, leaving it (and the harmless,
; merely redundant, re-derivation it performs) standing rather than
; aliasing two distinct values into one slot. Block/value names and
; shape trimmed (metadata, unrelated resource declarations, and the
; function's other, unrelated outer diamond removed) but otherwise
; preserved verbatim from the real failing case isolated via
; `FEME_DUMP_IR_PRELINEARIZE=1` against
; `dEQP-VK.spirv_assembly.instruction.graphics.float16.
; opcompositeextract.f16arr3_tessc`'s compiled hull-stage
; `main.patchconstant`; see `Roadmap.md`'s `ENV5(c)(2)` entry for the
; full writeup (which also corrects the prior session's disproven
; theory that this was a recurrence of `L452(d)`'s own, unrelated,
; `peelConstantFlowPredecessors` fix).

; CHECK-LABEL: define void @main.patchconstant(
; CHECK-NOT: unsupported
define void @main.patchconstant() #0 {
  %1 = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
  %2 = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
  %3 = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
  %4 = icmp eq i32 %2, 0
  %5 = icmp eq i32 %3, 0
  %6 = and i1 %4, %5
  br i1 %6, label %7, label %.Flow13_crit_edge

.Flow13_crit_edge:
  br label %Flow13

7:
  br label %8

Flow13:
  br label %spirv_fn_136.exit

8:
  %.0 = phi i32 [ %12, %Flow12._crit_edge ], [ 0, %7 ]
  %9 = icmp slt i32 %.0, 4
  br i1 %9, label %11, label %.Flow12_crit_edge

.Flow12_crit_edge:
  br label %Flow12

10:
  br label %Flow13

11:
  br label %NodeBlock5

Flow12:
  %12 = phi i32 [ %26, %spirv_fn_170.exit ], [ poison, %.Flow12_crit_edge ]
  %13 = phi i1 [ false, %spirv_fn_170.exit ], [ true, %.Flow12_crit_edge ]
  br i1 %13, label %10, label %Flow12._crit_edge

Flow12._crit_edge:
  br label %8

NodeBlock5:
  %Pivot6 = icmp sge i32 %.0, 1
  br i1 %Pivot6, label %NodeBlock, label %NodeBlock5.Flow_crit_edge

NodeBlock5.Flow_crit_edge:
  br label %Flow

NodeBlock:
  %Pivot = icmp sge i32 %.0, 2
  br i1 %Pivot, label %LeafBlock3, label %NodeBlock.Flow7_crit_edge

NodeBlock.Flow7_crit_edge:
  br label %Flow7

Flow:
  %14 = phi i1 [ %23, %Flow9 ], [ false, %NodeBlock5.Flow_crit_edge ]
  %15 = phi i1 [ false, %Flow9 ], [ true, %NodeBlock5.Flow_crit_edge ]
  br i1 %15, label %LeafBlock, label %Flow.Flow10_crit_edge

Flow.Flow10_crit_edge:
  br label %Flow10

LeafBlock3:
  %SwitchLeaf4 = icmp eq i32 %.0, 2
  br i1 %SwitchLeaf4, label %25, label %LeafBlock3.Flow8_crit_edge

LeafBlock3.Flow8_crit_edge:
  br label %Flow8

Flow9:
  br label %Flow

LeafBlock:
  %SwitchLeaf = icmp ne i32 %.0, 0
  br label %Flow10

Flow10:
  %16 = phi i1 [ true, %LeafBlock ], [ false, %Flow.Flow10_crit_edge ]
  %17 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %14, %Flow.Flow10_crit_edge ]
  br i1 %17, label %18, label %Flow10.Flow11_crit_edge

Flow10.Flow11_crit_edge:
  br label %Flow11

18:
  br label %Flow11

Flow11:
  %19 = phi i1 [ false, %18 ], [ %16, %Flow10.Flow11_crit_edge ]
  br i1 %19, label %20, label %Flow11.spirv_fn_170.exit_crit_edge

Flow11.spirv_fn_170.exit_crit_edge:
  br label %spirv_fn_170.exit

20:
  br label %spirv_fn_170.exit

Flow8:
  %21 = phi i1 [ false, %25 ], [ true, %LeafBlock3.Flow8_crit_edge ]
  br label %Flow7

22:
  br label %Flow9

Flow7:
  %23 = phi i1 [ %21, %Flow8 ], [ false, %NodeBlock.Flow7_crit_edge ]
  %24 = phi i1 [ false, %Flow8 ], [ true, %NodeBlock.Flow7_crit_edge ]
  br i1 %24, label %22, label %Flow7.Flow9_crit_edge

Flow7.Flow9_crit_edge:
  br label %Flow9

25:
  br label %Flow8

spirv_fn_170.exit:
  %26 = add i32 %.0, 1
  br label %Flow12

spirv_fn_136.exit:
  ret void
}

declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="hull" "feme.tessellation.output_control_points"="3" "hlsl.shader"="hull" }
