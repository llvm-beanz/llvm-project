; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | opt -passes=verify -S | FileCheck %s

; Roadmap L452(d)/(g)/(h): reduced from a real
; `dEQP-VK.spirv_assembly.type.scalar.i32.switch_comp` shape
; (`FEME_DUMP_IR_PRELINEARIZE=1`, hand-reduced). `Flow17` is a genuine
; `CondBrInst` diamond whose own clean, two-predecessor reconvergence point
; is `Flow18` -- but `Flow18`'s own single non-constant-condition
; predecessor is `peelConstantFlowPredecessors`'s own candidate: every
; *other* incoming value to `Flow18`'s flow-condition `phi` (just `%27`'s
; `24`-arm here) folds away as a provable constant, so naively peeling it
; would bypass `Flow18` entirely and force `Flow17`'s own `validate` walk
; to continue straight through into `Flow18._crit_edge`/block `20` --
; which `Flow17`'s *other* arm (through `Flow18` itself, left standing as
; a genuinely divergent `CondBrInst`) independently, and unavoidably,
; visits too, since `Flow18`'s own remaining successor is by construction
; one of `Flow17`'s own two arms' targets. `DiamondFlattener::validate`'s
; `MergeVisited` double-visit check (this milestone's hard limit on nested
; diamond-of-diamond reconvergence) would catch this and leave the whole
; region diagnosed, not silently miscompiled, were the dangerous peel ever
; allowed to proceed.
;
; `peelWouldDestroyAncestorReconvergencePoint`'s own scoped dry run (see
; its comment) is what must actually prevent `Flow18` from being
; destroyed in the first place: it speculatively performs the exact edge
; redirect this peel would make, recomputes the post-dominator tree, and
; runs a throwaway, read-only `DiamondFlattener::validate` probe before
; undoing both -- the same answer `validate`'s own real, later run would
; give, computed early enough to bail out of exactly this one dangerous
; peel while leaving every other peel free to proceed. Blocking only the
; peel that would actually destroy `Flow18` lets the real
; `DiamondFlattener` fixed-point sweep flatten `Flow17`'s diamond (with
; `Flow18` still standing as its clean reconvergence point) normally
; afterward, so the whole function fully linearizes -- this is also a
; real-world improvement over the four prior sessions' broader, purely
; structural purity heuristic, which (while it happened to also block
; this exact peel) was conservative enough elsewhere in this same
; function to leave an unrelated, otherwise-flattenable diamond
; undiagnosed-but-unsupported instead.

; CHECK: define void @main(
; CHECK: ret void
define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #2 {
  br label %loop

loop:                                             ; preds = %Flow19._crit_edge, %0
  %.05 = phi <4 x float> [ <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00>, %0 ], [ %11, %Flow19._crit_edge ]
  %.0 = phi i32 [ 0, %0 ], [ %10, %Flow19._crit_edge ]
  %2 = icmp slt i32 %.0, 10
  br i1 %2, label %3, label %.Flow19_crit_edge

.Flow19_crit_edge:                                ; preds = %loop
  br label %Flow19

3:                                                ; preds = %loop
  %4 = zext i32 %.0 to i64
  %5 = mul i64 %4, 4
  %6 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %5, i1 true)
  %7 = zext i32 %.0 to i64
  %8 = mul i64 %7, 4
  %9 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 %8, i1 true)
  br label %NodeBlock10

Flow19:                                           ; preds = %.Flow19_crit_edge, %39
  %10 = phi i32 [ %40, %39 ], [ poison, %.Flow19_crit_edge ]
  %11 = phi <4 x float> [ %.1, %39 ], [ poison, %.Flow19_crit_edge ]
  %12 = phi i1 [ false, %39 ], [ true, %.Flow19_crit_edge ]
  br i1 %12, label %spirv_fn_98.exit, label %Flow19._crit_edge

Flow19._crit_edge:                                ; preds = %Flow19
  br label %loop

NodeBlock10:                                      ; preds = %3
  %Pivot11 = icmp sge i32 %6, 3210
  br i1 %Pivot11, label %NodeBlock, label %NodeBlock10.Flow12_crit_edge

NodeBlock10.Flow12_crit_edge:                     ; preds = %NodeBlock10
  br label %Flow12

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp sge i32 %6, 268438669
  br i1 %Pivot, label %LeafBlock8, label %NodeBlock.Flow13_crit_edge

NodeBlock.Flow13_crit_edge:                       ; preds = %NodeBlock
  br label %Flow13

Flow12:                                           ; preds = %NodeBlock10.Flow12_crit_edge, %Flow15
  %13 = phi i1 [ %30, %Flow15 ], [ poison, %NodeBlock10.Flow12_crit_edge ]
  %14 = phi i1 [ %31, %Flow15 ], [ false, %NodeBlock10.Flow12_crit_edge ]
  %15 = phi i1 [ false, %Flow15 ], [ true, %NodeBlock10.Flow12_crit_edge ]
  br i1 %15, label %LeafBlock, label %Flow12.Flow17_crit_edge

Flow12.Flow17_crit_edge:                          ; preds = %Flow12
  br label %Flow17

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %6, 268438669
  br i1 %SwitchLeaf9, label %37, label %LeafBlock8.Flow14_crit_edge

LeafBlock8.Flow14_crit_edge:                      ; preds = %LeafBlock8
  br label %Flow14

Flow14:                                           ; preds = %LeafBlock8.Flow14_crit_edge, %37
  %16 = phi i1 [ %38, %37 ], [ poison, %LeafBlock8.Flow14_crit_edge ]
  %17 = phi i1 [ false, %37 ], [ true, %LeafBlock8.Flow14_crit_edge ]
  br label %Flow13

LeafBlock6:                                       ; preds = %Flow13
  %SwitchLeaf7 = icmp eq i32 %6, 3210
  br i1 %SwitchLeaf7, label %32, label %LeafBlock6.Flow16_crit_edge

LeafBlock6.Flow16_crit_edge:                      ; preds = %LeafBlock6
  br label %Flow16

Flow16:                                           ; preds = %LeafBlock6.Flow16_crit_edge, %32
  %18 = phi i1 [ %33, %32 ], [ poison, %LeafBlock6.Flow16_crit_edge ]
  %19 = phi i1 [ false, %32 ], [ true, %LeafBlock6.Flow16_crit_edge ]
  br label %Flow15

LeafBlock:                                        ; preds = %Flow12
  %SwitchLeaf = icmp ne i32 %6, -3221
  br label %Flow17

20:                                               ; preds = %Flow18._crit_edge, %28
  %21 = phi i1 [ %29, %28 ], [ %26, %Flow18._crit_edge ]
  %.inv = xor i1 %21, true
  br i1 %.inv, label %43, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %20
  br label %Flow

Flow17:                                           ; preds = %Flow12.Flow17_crit_edge, %LeafBlock
  %22 = phi i1 [ true, %LeafBlock ], [ false, %Flow12.Flow17_crit_edge ]
  %23 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %14, %Flow12.Flow17_crit_edge ]
  br i1 %23, label %24, label %Flow17.Flow18_crit_edge

Flow17.Flow18_crit_edge:                          ; preds = %Flow17
  br label %Flow18

24:                                               ; preds = %Flow17
  %25 = icmp eq i32 %9, 10
  br label %Flow18

Flow18:                                           ; preds = %Flow17.Flow18_crit_edge, %24
  %26 = phi i1 [ %25, %24 ], [ %13, %Flow17.Flow18_crit_edge ]
  %27 = phi i1 [ false, %24 ], [ %22, %Flow17.Flow18_crit_edge ]
  br i1 %27, label %28, label %Flow18._crit_edge

Flow18._crit_edge:                                ; preds = %Flow18
  br label %20

28:                                               ; preds = %Flow18
  %29 = icmp eq i32 %9, 100
  br label %20

Flow15:                                           ; preds = %Flow13.Flow15_crit_edge, %Flow16
  %30 = phi i1 [ %18, %Flow16 ], [ %34, %Flow13.Flow15_crit_edge ]
  %31 = phi i1 [ %19, %Flow16 ], [ %35, %Flow13.Flow15_crit_edge ]
  br label %Flow12

32:                                               ; preds = %LeafBlock6
  %33 = icmp eq i32 %9, 110
  br label %Flow16

Flow13:                                           ; preds = %NodeBlock.Flow13_crit_edge, %Flow14
  %34 = phi i1 [ %16, %Flow14 ], [ poison, %NodeBlock.Flow13_crit_edge ]
  %35 = phi i1 [ %17, %Flow14 ], [ false, %NodeBlock.Flow13_crit_edge ]
  %36 = phi i1 [ false, %Flow14 ], [ true, %NodeBlock.Flow13_crit_edge ]
  br i1 %36, label %LeafBlock6, label %Flow13.Flow15_crit_edge

Flow13.Flow15_crit_edge:                          ; preds = %Flow13
  br label %Flow15

37:                                               ; preds = %LeafBlock8
  %38 = icmp eq i32 %9, 120
  br label %Flow14

39:                                               ; preds = %Flow._crit_edge, %42
  %.1 = phi <4 x float> [ %.05, %42 ], [ <float 5.000000e-01, float 5.000000e-01, float 5.000000e-01, float 1.000000e+00>, %Flow._crit_edge ]
  %40 = add i32 %.0, 1
  br label %Flow19

Flow:                                             ; preds = %.Flow_crit_edge, %43
  %41 = phi i1 [ false, %43 ], [ true, %.Flow_crit_edge ]
  br i1 %41, label %42, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %39

42:                                               ; preds = %Flow
  br label %39

43:                                               ; preds = %20
  %44 = add i32 %.0, 10
  br label %Flow

spirv_fn_98.exit:                                 ; preds = %Flow19
  %45 = fcmp oeq <4 x float> <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00>, %.05
  %46 = call i1 @llvm.vector.reduce.and.v4i1(<4 x i1> %45)
  %47 = select i1 %46, i32 1, i32 0
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 0, i32 %47, i1 true)
  ret void
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i1 @llvm.vector.reduce.and.v4i1(<4 x i1>) #0

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: write)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1) #4

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #2 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
attributes #3 = { nounwind willreturn memory(argmem: read) }
attributes #4 = { nounwind willreturn memory(argmem: write) }
