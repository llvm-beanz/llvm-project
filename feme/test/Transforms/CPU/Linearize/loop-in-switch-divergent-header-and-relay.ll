; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; Roadmap L419(h): reduced (dropping only unused globals/metadata, not any
; block/instruction that actually affects this pass's analysis) from
; `dEQP-VK.glsl.switch.loop_in_switch.while_loop_in_switch_dynamic_fragment`
; (one of this group's 4 still-failing cases, alongside its `for_`-loop and
; `*_vertex` siblings).
;
; The shape: a `switch` lowered via `StructurizeCFG`'s usual
; `NodeBlock`/`LeafBlock`/`LeafBlock1` pivot-compare dispatch, with one case
; arm (`33` -> `41`) containing a `while` loop whose header (`41`) holds a
; genuinely divergent, per-fragment trip-count check (`%42`, depending on
; `%38`, itself derived from a fragment-varying input load). Crucially,
; neither of `41`'s own two successors (`43`, the "keep looping" arm, or
; `.Flow_crit_edge`, the "stop" arm) reaches this loop's real exit block
; (`Flow3`) directly, or through a *pure* (single-successor) relay chain --
; both instead first funnel into a single shared `Flow` block, which is
; *also* reused as the merge point for this switch's own, entirely
; unrelated case-dispatch scaffolding (`Flow3` -> `Flow5` -> `Flow4` ->
; `Flow6`/`Flow7` -> the function's single shared return block, `16`).
; `Flow` itself re-splits on a phi (`%15`) that is purely a relabeling of
; `41`'s own `%42`, and *is* correctly recognized as this cycle's single
; real exit check (`CheckBlock`, via `matchExitCheckWithRelay`'s direct
; `matchExitCheck` hit, since `Flow`'s own first successor literally is
; `Flow3`) -- but `41`'s own divergent branch is never itself recognized as
; a (second) real exit check: `matchExitCheck`/`recoverRelayExitCheck`
; both require a *direct or purely-relayed* path to `ExitBlock`, and `41`'s
; own successors only reach `Flow3` indirectly, through `Flow`'s own
; further `CondBr` -- so `HeaderExit` stays null, and `HeaderDivergent`
; (computed only from `HeaderExit`) reads `false`, even though `41`'s real
; check is, in fact, divergent.
;
; Before this fix, `collectUniformPassThroughRegion`'s walk from `41`
; (`Header`) forward to `Flow` (`CheckBlock`) hit `41`'s own un-recognized
; divergent branch partway through and failed, surfacing a misleading
; "internal branch in 'Flow'" diagnostic that incorrectly blamed `Flow`
; itself for a shape that is actually unsupported because of a *second*,
; separately-divergent check at `41`. This is the same fundamentally
; unsupported "two genuinely divergent exit checks" shape
; `RealExitCandidates.size() > 1` above already diagnoses clearly; detect
; it here too (by directly checking `Header`/`Latch`'s own terminator,
; rather than relying on `HeaderExit`/`LatchExit`) and emit that same,
; more accurate message instead.
;
; Not yet fixed when this comment was first written: actually *lowering*
; this shape (masking `41`'s own divergent trip-count check as a
; first-stage filter ahead of `Flow`'s own check) remained unimplemented,
; and a prior version of this test asserted the diagnostic above instead.
;
; Roadmap L419(ac): now fixed. `recoverRelayExitCheck`'s generalized chase
; (roadmap L419(ab)) resolves `41`'s own divergent branch through `Flow`
; (a genuine relay hop, recorded into `ExitCheck::RelayChain`) to the real
; `ExitBlock`, admitting `41` into the usual `HeaderDivergent` handling.
; But masking `41`'s check alone is not sufficient for correctness: `Flow`
; still owns real, multi-valued phis (`%13`/`%14`/`%15`) distinguishing
; "arrived via `41`'s own exit" from "arrived via the loop's continue
; edge" -- once `41`'s `CondBr` is erased and replaced with an
; unconditional branch, `Flow`'s only predecessor from `41` vanishes,
; silently orphaning that distinction once `EliminateUnreachableBlocks`
; eventually collapses it (confirmed, via direct IR inspection, to
; corrupt already-exited lanes' final pixel value -- the "Image mismatch"
; CTS failure this exact shape produces). `freezeRelayChainExitValues`
; fixes this by `select`-wrapping `Flow`'s own phis on the same per-lane
; "still looping" guard mask `41`'s own masking already computed, picking
; the orphaned exit edge's value for any lane that already left; and
; because the orphaned value here is `41`'s own raw loop-carried `%.010`
; phi (not a derived instruction), `freezeLoopCarriedValues` additionally
; applies its usual sticky, self-referential freeze to `%.010`'s own
; backedge too (via the new `RelayCaptured` parameter), so a lane that
; already exited keeps its own frozen value forever, not whatever the
; loop's still-executing body recomputes for later iterations.
;
; Roadmap ENV6(c)(2)(b): `freezeRelayChainExitValues`'s own `select` for
; this phi's kept (`43`) edge is built at `43`'s own terminator, not
; `Flow`'s (any value already proven to dominate `Flow` also dominates
; every one of `Flow`'s own predecessors, including `43`, by a general
; dominance argument -- not the other way around). `43` (printed here as
; block `33`, LLVM's own anonymous-value renumbering after the pass runs)
; is emitted *after* `Flow._crit_edge` in this function's final block
; order even though it dominates it, so the two `CHECK`s below are kept
; in that same (textual, not CFG) order.

; CHECK: Flow._crit_edge:
; CHECK:   %.010.frozen = select i1 %active.check.live, <3 x float> %.relay.frozen{{[0-9]*}}, <3 x float> %.010
; CHECK: 33:
; CHECK:   %.relay.frozen{{[0-9]*}} = select i1 %active.header.check.live, <3 x float> %{{[0-9]+}}, <3 x float> %.010
define void @main() #0 {
  %1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %2 = insertelement <4 x float> poison, float %1, i64 0
  %3 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %4 = insertelement <4 x float> %2, float %3, i64 1
  %5 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %6 = insertelement <4 x float> %4, float %5, i64 2
  %7 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %8 = insertelement <4 x float> %6, float %7, i64 3
  %9 = fmul float %5, 1.500000e+00
  %10 = fadd float %9, 2.000000e+00
  %11 = call float @llvm.floor.f32(float %10)
  %12 = fptosi float %11 to i32
  br label %NodeBlock

NodeBlock:                                        ; preds = %0
  %Pivot = icmp sge i32 %12, 1
  br i1 %Pivot, label %LeafBlock1, label %NodeBlock.Flow4_crit_edge

NodeBlock.Flow4_crit_edge:                        ; preds = %NodeBlock
  br label %Flow4

LeafBlock1:                                       ; preds = %NodeBlock
  %SwitchLeaf2 = icmp sle i32 %12, 2
  br i1 %SwitchLeaf2, label %33, label %LeafBlock1.Flow5_crit_edge

LeafBlock1.Flow5_crit_edge:                       ; preds = %LeafBlock1
  br label %Flow5

LeafBlock:                                        ; preds = %Flow4
  %SwitchLeaf = icmp ne i32 %12, 0
  br label %Flow6

Flow:                                             ; preds = %.Flow_crit_edge, %43
  %13 = phi i32 [ %44, %43 ], [ poison, %.Flow_crit_edge ]
  %14 = phi <3 x float> [ %45, %43 ], [ %.010, %.Flow_crit_edge ]
  %15 = phi i1 [ false, %43 ], [ true, %.Flow_crit_edge ]
  br i1 %15, label %Flow3, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %41

Flow3:                                            ; preds = %Flow
  br label %Flow5

16:                                               ; preds = %Flow7._crit_edge, %28
  %.011 = phi <3 x float> [ %26, %Flow7._crit_edge ], [ %29, %28 ]
  %17 = shufflevector <3 x float> %.011, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
  %18 = insertelement <4 x float> %17, float 1.000000e+00, i64 3
  %19 = extractelement <4 x float> %18, i64 0
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %19, i32 0)
  %20 = extractelement <4 x float> %18, i64 1
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %20, i32 0)
  %21 = extractelement <4 x float> %18, i64 2
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %21, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float 1.000000e+00, i32 0)
  ret void

Flow6:                                            ; preds = %Flow4.Flow6_crit_edge, %LeafBlock
  %22 = phi i1 [ true, %LeafBlock ], [ false, %Flow4.Flow6_crit_edge ]
  %23 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %31, %Flow4.Flow6_crit_edge ]
  br i1 %23, label %24, label %Flow6.Flow7_crit_edge

Flow6.Flow7_crit_edge:                            ; preds = %Flow6
  br label %Flow7

24:                                               ; preds = %Flow6
  %25 = shufflevector <4 x float> %8, <4 x float> poison, <3 x i32> <i32 2, i32 1, i32 0>
  br label %Flow7

Flow7:                                            ; preds = %Flow6.Flow7_crit_edge, %24
  %26 = phi <3 x float> [ %25, %24 ], [ %30, %Flow6.Flow7_crit_edge ]
  %27 = phi i1 [ false, %24 ], [ %22, %Flow6.Flow7_crit_edge ]
  br i1 %27, label %28, label %Flow7._crit_edge

Flow7._crit_edge:                                 ; preds = %Flow7
  br label %16

28:                                               ; preds = %Flow7
  %29 = shufflevector <4 x float> %8, <4 x float> poison, <3 x i32> <i32 0, i32 1, i32 2>
  br label %16

Flow4:                                            ; preds = %NodeBlock.Flow4_crit_edge, %Flow5
  %30 = phi <3 x float> [ %39, %Flow5 ], [ poison, %NodeBlock.Flow4_crit_edge ]
  %31 = phi i1 [ %40, %Flow5 ], [ false, %NodeBlock.Flow4_crit_edge ]
  %32 = phi i1 [ false, %Flow5 ], [ true, %NodeBlock.Flow4_crit_edge ]
  br i1 %32, label %LeafBlock, label %Flow4.Flow6_crit_edge

Flow4.Flow6_crit_edge:                            ; preds = %Flow4
  br label %Flow6

33:                                               ; preds = %LeafBlock1
  %34 = shufflevector <4 x float> %8, <4 x float> poison, <3 x i32> <i32 1, i32 2, i32 3>
  %35 = fmul float %5, 1.500000e+00
  %36 = fadd float %35, 2.000000e+00
  %37 = call float @llvm.floor.f32(float %36)
  %38 = fptosi float %37 to i32
  br label %41

Flow5:                                            ; preds = %LeafBlock1.Flow5_crit_edge, %Flow3
  %39 = phi <3 x float> [ %14, %Flow3 ], [ poison, %LeafBlock1.Flow5_crit_edge ]
  %40 = phi i1 [ false, %Flow3 ], [ true, %LeafBlock1.Flow5_crit_edge ]
  br label %Flow4

41:                                               ; preds = %Flow._crit_edge, %33
  %.010 = phi <3 x float> [ %14, %Flow._crit_edge ], [ %34, %33 ]
  %.0 = phi i32 [ %13, %Flow._crit_edge ], [ 0, %33 ]
  %42 = icmp slt i32 %.0, %38
  br i1 %42, label %43, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %41
  br label %Flow

43:                                               ; preds = %41
  %44 = add i32 %.0, 1
  %45 = shufflevector <3 x float> %.010, <3 x float> poison, <3 x i32> <i32 2, i32 1, i32 0>
  br label %Flow
}

declare float @llvm.floor.f32(float)
declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
