; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,verify -S %s -o /dev/null

; Roadmap ENV6(e)(7): a real, captured pre-`feme-cpu-linearize` IR
; reduction of `dEQP-VK.graphicsfuzz.cov-x86-isel-lowering-negative-
; left-shift` (two nested, both `HeaderDivergent`, uniform-trip-count
; `for` loops: the outer loop merely iterates, the inner loop's own body
; contains a `switch`-lowered (`NodeBlock`/`LeafBlock`/`LeafBlock1`)
; divergent diamond). Found via `FEME_DUMP_IR_PRELINEARIZE=1` capturing
; the real pre-pass IR, then reproduced standalone (no CTS/Vulkan
; pipeline needed) through `feme-opt -passes=feme-cpu-linearize,verify`
; directly.
;
; Root cause: when `LoopLinearizer::linearizeCyclePostOrder` processes
; the *inner* cycle first, its own `HeaderDivergent` handling rewrites
; the inner header's own exit-check `CondBrInst` into a plain
; `UncondBrInst` (see `stayInLoop`), after already fully flattening its
; own body's divergent diamond (the switch lowering above) into
; `select`s. When the *outer* cycle is linearized next, its own
; `DiamondFlattener::flatten` walk reaches the inner loop by stepping
; into its body region and arrives at the inner header -- but the
; existing `GetAlreadyLinearizedCycleExit` shortcut (meant to let an
; enclosing cycle's walk jump straight from a nested, already-linearized
; child cycle to that child's one exit block, without ever re-entering
; its finalized interior) was only ever checked when the reached block's
; own terminator was *still* a `CondBrInst` matching
; `isLoopControlOrHeaderRelayEdge` -- true for the inner cycle's own
; still-conditional relay/latch block (`Flow9._crit_edge` below), but
; false for the inner header itself, whose own terminator had already
; become an `UncondBrInst`. The outer walk therefore fell through to
; ordinary unconditional-branch stepping, walking one block at a time
; through the *entire* already-finalized inner loop body a second time,
; re-flattening its `NodeBlock`/`LeafBlock`/`LeafBlock1` switch diamond
; from scratch with the outer loop's own (different, narrower) mask --
; and, because a masked resource load's mask operand had meanwhile been
; narrowed from a plain `true` constant to a non-constant value by this
; same re-walk's own earlier `applyStageMasks` call, a branch condition
; `flatten`'s first pass had correctly classified uniform got
; reclassified divergent on this second pass, producing a second,
; conflicting set of `select`s. The final `Narrow` step (retroactively
; narrowing the inner loop's own entry mask once the outer walk finally
; reached the inner loop's relay block) then populated the inner loop's
; own loop-carried `active.live`/`active.sideeffect` phi with one
; stale and one non-dominating incoming value, tripping a
; `verifyModule` "Instruction does not dominate all uses!" failure on
; exactly that phi.
;
; Fix: check `GetAlreadyLinearizedCycleExit` (in both `validate` and
; `flatten`) as soon as the reached block's own terminator is *not* a
; `CondBrInst`, not only when it still is -- shortcutting straight past
; an already-uncond-ified child header exactly the same way the
; still-conditional relay/latch block already was, so an enclosing
; cycle's own walk never re-enters (let alone re-mutates) a nested,
; already-linearized child cycle's interior at all.
;
; This reduction is deliberately kept close to its real capture (see
; `.instructions.md`'s `FEME_DUMP_IR_PRELINEARIZE`/`FEME_OPT_DUMP_INVALID`
; write-up for the technique) rather than hand-minimized further, since
; the exact nested-loop/switch-diamond shape is what triggers this bug.

@spirv_var_18.str = private constant [13 x i8] c"spirv_var_18\00"
@spirv_var_34.str = private constant [13 x i8] c"spirv_var_34\00"
@_GLF_color = external addrspace(8) global <4 x float>, !spirv.Decorations !1

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 !feme.signature !2 {
  %1 = alloca [2 x float], align 4
  %2 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  store float %2, ptr %1, align 4
  %3 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %4 = getelementptr inbounds nuw i8, ptr %1, i64 4
  store float %3, ptr %4, align 4
  %5 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  br label %6

6:                                                ; preds = %Flow10._crit_edge, %0
  %.08 = phi i32 [ %5, %0 ], [ %18, %Flow10._crit_edge ]
  %7 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 48, i1 true)
  %8 = icmp slt i32 %.08, %7
  br i1 %8, label %16, label %.Flow10_crit_edge

.Flow10_crit_edge:                                ; preds = %6
  br label %Flow10

9:                                                ; preds = %Flow10
  %10 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %11 = sext i32 %10 to i64
  %12 = getelementptr [4 x i8], ptr %1, i64 %11
  %13 = load float, ptr %12, align 4
  %14 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %15 = fcmp oeq float %13, %14
  %.inv3 = xor i1 %15, true
  br i1 %15, label %40, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %9
  br label %Flow

16:                                               ; preds = %6
  %17 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  br label %20

Flow10:                                           ; preds = %.Flow10_crit_edge, %23
  %18 = phi i32 [ %24, %23 ], [ poison, %.Flow10_crit_edge ]
  %19 = phi i1 [ false, %23 ], [ true, %.Flow10_crit_edge ]
  br i1 %19, label %9, label %Flow10._crit_edge

Flow10._crit_edge:                                ; preds = %Flow10
  br label %6

20:                                               ; preds = %Flow9._crit_edge, %16
  %.0 = phi i32 [ %28, %Flow9._crit_edge ], [ %17, %16 ]
  %21 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i1 true)
  %22 = icmp slt i32 %.0, %21
  br i1 %22, label %27, label %.Flow9_crit_edge

.Flow9_crit_edge:                                 ; preds = %20
  br label %Flow9

23:                                               ; preds = %Flow9
  %24 = add i32 %.08, 1
  br label %Flow10

Flow5:                                            ; preds = %LeafBlock1.Flow5_crit_edge, %32
  br label %Flow8

Flow6:                                            ; preds = %.Flow6_crit_edge, %36
  br label %Flow7

Flow7:                                            ; preds = %LeafBlock.Flow7_crit_edge, %Flow6
  br label %25

25:                                               ; preds = %Flow8._crit_edge, %Flow7
  %26 = add i32 %.0, 1
  br label %Flow9

27:                                               ; preds = %20
  br label %NodeBlock

Flow9:                                            ; preds = %.Flow9_crit_edge, %25
  %28 = phi i32 [ %26, %25 ], [ poison, %.Flow9_crit_edge ]
  %29 = phi i1 [ false, %25 ], [ true, %.Flow9_crit_edge ]
  br i1 %29, label %23, label %Flow9._crit_edge

Flow9._crit_edge:                                 ; preds = %Flow9
  br label %20

NodeBlock:                                        ; preds = %27
  %Pivot = icmp sge i32 %.0, 1
  br i1 %Pivot, label %LeafBlock1, label %NodeBlock.Flow8_crit_edge

NodeBlock.Flow8_crit_edge:                        ; preds = %NodeBlock
  br label %Flow8

LeafBlock1:                                       ; preds = %NodeBlock
  %SwitchLeaf2 = icmp eq i32 %.0, 1
  br i1 %SwitchLeaf2, label %32, label %LeafBlock1.Flow5_crit_edge

LeafBlock1.Flow5_crit_edge:                       ; preds = %LeafBlock1
  br label %Flow5

Flow8:                                            ; preds = %NodeBlock.Flow8_crit_edge, %Flow5
  %30 = phi i1 [ false, %Flow5 ], [ true, %NodeBlock.Flow8_crit_edge ]
  br i1 %30, label %LeafBlock, label %Flow8._crit_edge

Flow8._crit_edge:                                 ; preds = %Flow8
  br label %25

LeafBlock:                                        ; preds = %Flow8
  %SwitchLeaf = icmp eq i32 %.0, 0
  br i1 %SwitchLeaf, label %31, label %LeafBlock.Flow7_crit_edge

LeafBlock.Flow7_crit_edge:                        ; preds = %LeafBlock
  br label %Flow7

31:                                               ; preds = %LeafBlock
  %.not = icmp eq i32 %.08, -2147483648
  br i1 %.not, label %36, label %.Flow6_crit_edge

.Flow6_crit_edge:                                 ; preds = %31
  br label %Flow6

32:                                               ; preds = %LeafBlock1
  %33 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %34 = sext i32 %.08 to i64
  %35 = getelementptr [4 x i8], ptr %1, i64 %34
  store float %33, ptr %35, align 4
  br label %Flow5

36:                                               ; preds = %31
  %37 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %38 = sext i32 %.08 to i64
  %39 = getelementptr [4 x i8], ptr %1, i64 %38
  store float %37, ptr %39, align 4
  br label %Flow6

40:                                               ; preds = %9
  %41 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %42 = sext i32 %41 to i64
  %43 = getelementptr [4 x i8], ptr %1, i64 %42
  %44 = load float, ptr %43, align 4
  %45 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %46 = fcmp une float %44, %45
  br label %Flow

47:                                               ; preds = %Flow4._crit_edge, %54
  %storemerge = phi <4 x float> [ %52, %Flow4._crit_edge ], [ %66, %54 ]
  %48 = extractelement <4 x float> %storemerge, i64 0
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %48, i32 0)
  %49 = extractelement <4 x float> %storemerge, i64 1
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %49, i32 0)
  %50 = extractelement <4 x float> %storemerge, i64 2
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %50, i32 0)
  %51 = extractelement <4 x float> %storemerge, i64 3
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %51, i32 0)
  ret void

Flow4:                                            ; preds = %Flow.Flow4_crit_edge, %.critedge
  %52 = phi <4 x float> [ %72, %.critedge ], [ poison, %Flow.Flow4_crit_edge ]
  %53 = phi i1 [ false, %.critedge ], [ %67, %Flow.Flow4_crit_edge ]
  br i1 %53, label %54, label %Flow4._crit_edge

Flow4._crit_edge:                                 ; preds = %Flow4
  br label %47

54:                                               ; preds = %Flow4
  %55 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %56 = sitofp i32 %55 to float
  %57 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %58 = sitofp i32 %57 to float
  %59 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %60 = sitofp i32 %59 to float
  %61 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %62 = sitofp i32 %61 to float
  %63 = insertelement <4 x float> poison, float %56, i64 0
  %64 = insertelement <4 x float> %63, float %58, i64 1
  %65 = insertelement <4 x float> %64, float %60, i64 2
  %66 = insertelement <4 x float> %65, float %62, i64 3
  br label %47

Flow:                                             ; preds = %.Flow_crit_edge, %40
  %67 = phi i1 [ true, %40 ], [ false, %.Flow_crit_edge ]
  %68 = phi i1 [ %46, %40 ], [ %.inv3, %.Flow_crit_edge ]
  br i1 %68, label %.critedge, label %Flow.Flow4_crit_edge

Flow.Flow4_crit_edge:                             ; preds = %Flow
  br label %Flow4

.critedge:                                        ; preds = %Flow
  %69 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %70 = sitofp i32 %69 to float
  %71 = insertelement <4 x float> poison, float %70, i64 0
  %72 = shufflevector <4 x float> %71, <4 x float> poison, <4 x i32> zeroinitializer
  br label %Flow4
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #1

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #1

attributes #0 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #1 = { nounwind willreturn memory(argmem: read) }

!feme.cpu.resources = !{!3}
!feme.cpu.bound_resources = !{!4}

!0 = !{i32 30, i32 0}
!1 = !{!0}
!2 = !{[116 x i8] c"\09\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
!3 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!4 = !{!"main", i32 2, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0}

; CHECK-LABEL: define void @main(
; The inner loop's own loop-carried `active.live`/`active.sideeffect`
; mask phi (at the inner header) must have both incoming values actually
; dominate their respective predecessor blocks: the non-backedge edge
; (from the outer loop's own body, `%16` above) must carry the *outer*
; loop's own header mask (defined back at the outer header, dominating
; everything in its body), not a value defined deep inside the inner
; loop's own body (which cannot dominate `%16`); the backedge (from the
; inner loop's own relay/latch, `Flow9._crit_edge` above) must carry the
; inner loop's own, now up-to-date, per-iteration mask, not a stale
; value computed before the inner loop's body ran.
; CHECK: %active.live = phi i1 [ %active.header.live{{[0-9]+}}, %16 ], [ %active.header.live, %Flow9._crit_edge ]
; CHECK-NEXT: %active.sideeffect = phi i1 [ %active.header.sideeffect{{[0-9]+}}, %16 ], [ %active.header.sideeffect, %Flow9._crit_edge ]
