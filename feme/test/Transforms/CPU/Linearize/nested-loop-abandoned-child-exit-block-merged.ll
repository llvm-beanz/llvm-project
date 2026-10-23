; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap L463: a real, captured pre-`feme-cpu-linearize` IR reduction of
; `dEQP-VK.graphicsfuzz.cov-loop-condition-divide-by-uniform-always-false`'s
; fragment shader (two nested `for` loops: the outer counts to 9, the
; inner counts to 10 and `continue`s once a resource-loaded bound is
; exceeded; the inner loop's own body also has a genuinely divergent
; `if` guarding a uniform-looking-but-not `fdiv` comparison that makes
; this milestone's `DiamondFlattener::validate` diagnose the inner loop's
; own body shape as unsupported and leave it alone). Captured via
; `FEME_DUMP_IR_PRELINEARIZE=1` against the real ICD and reproduced
; standalone with a plain `feme-opt -passes=feme-cpu-linearize` run,
; independent of the full Vulkan CTS/ICD pipeline.
;
; Root cause: the inner loop's own `linearizeCycle` call runs first
; (post-order), folds away one of its own redundant "Flow" blocks (see
; `foldRedundantFlowBlock`), and then correctly, cleanly diagnoses its own
; body shape as unsupported and bails out, leaving the inner loop
; completely unmasked -- its own cached exit block (`ExitBlocksByCycle`,
; "Roadmap L197") is still a real, well-formed block at that point. The
; *outer* loop's own `linearizeCycle` call then runs: its own fold
; helpers (`foldRedundantFlowBlocksInCycle`,
; `peelConstantFlowPredecessorsInCycle`,
; `mergeTrivialRelayBlocksInCycle`) are deliberately scoped via
; `CI.contains`, which is true for every block of the still-nested inner
; cycle too, not just the outer's own "direct" body -- and one of those
; folds (`mergeTrivialRelayBlocksInCycle`'s `MergeBlockIntoPredecessor`)
; ends up merging the inner loop's own cached exit block away as
; collateral damage, once a sibling fold has left it a phi-less,
; single-predecessor relay stub. The outer loop's own `validate` walk
; then reaches the inner loop's header, recognizes it (correctly) as an
; already-attempted child cycle, and asks `GetAlreadyLinearizedCycleExit`
; for that child's cached exit block -- which, before this fix, was a
; raw, now-dangling `BasicBlock *` into freed memory, read straight into
; a recursive `validate` call that then asserted on the freed block's
; `getTerminator()`. Fixed by making `ExitBlocksByCycle` `WeakTrackingVH`-
; valued (mirroring `HeaderActiveMasks`'s own "Roadmap L440" fix for the
; identical bug class), so an entry erased out from under the cache
; safely nulls out instead of dangling, letting
; `GetAlreadyLinearizedCycleExit` fall back to its own existing, already-
; graceful "not yet supported here" path instead of reading freed memory.
; Confirmed to abort (`SIGABRT`, `getTerminator`'s "Block does not have a
; terminator set!" assertion) without the fix, and to produce this clean
; diagnostic instead with it.

; CHECK: feme-cpu-linearize: function 'main': loop at '{{.*}}' has an internal branch in a body shape this milestone does not yet lower

declare i1 @llvm.is.fpclass.f32(float, i32 immarg)
declare float @llvm.copysign.f32(float, float)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
  %1 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %2 = sitofp i32 %1 to float
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %2, i32 0)
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %2, i32 0)
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %2, i32 0)
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %2, i32 0)
  br label %5

5:                                                ; preds = %Flow6._crit_edge, %0
  %.05 = phi i32 [ 1, %0 ], [ %53, %Flow6._crit_edge ]
  %6 = icmp slt i32 %.05, 9
  br i1 %6, label %._crit_edge, label %.Flow6_crit_edge

.Flow6_crit_edge:                                 ; preds = %5
  br label %Flow6

._crit_edge:                                      ; preds = %5
  br label %20

common.ret:                                       ; preds = %loop.exit.guard.common.ret_crit_edge, %7
  ret void

7:                                                ; preds = %loop.exit.guard
  %8 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %9 = sitofp i32 %8 to float
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %9, i32 0)
  br label %common.ret

20:                                               ; preds = %Flow._crit_edge, %._crit_edge
  %.0 = phi i32 [ %31, %Flow._crit_edge ], [ 1, %._crit_edge ]
  %21 = icmp slt i32 %.0, 10
  br i1 %21, label %28, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %20
  br label %Flow

22:                                               ; preds = %loop.exit.guard1
  %23 = add i32 %.05, 1
  br label %Flow7

Flow4:                                            ; preds = %.Flow4_crit_edge, %34
  %24 = phi i1 [ false, %34 ], [ true, %.Flow4_crit_edge ]
  %25 = phi i1 [ %48, %34 ], [ %30, %.Flow4_crit_edge ]
  br i1 %25, label %26, label %Flow4.Flow5_crit_edge

Flow4.Flow5_crit_edge:                            ; preds = %Flow4
  br label %Flow5

26:                                               ; preds = %Flow4
  %27 = add i32 %.0, 1
  br label %Flow5

28:                                               ; preds = %20
  %29 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %30 = icmp sgt i32 %.05, %29
  %.inv = xor i1 %30, true
  br i1 %.inv, label %34, label %.Flow4_crit_edge

.Flow4_crit_edge:                                 ; preds = %28
  br label %Flow4

Flow:                                             ; preds = %.Flow_crit_edge, %Flow5
  %31 = phi i32 [ %51, %Flow5 ], [ poison, %.Flow_crit_edge ]
  %32 = phi i1 [ %24, %Flow5 ], [ true, %.Flow_crit_edge ]
  %33 = phi i1 [ %52, %Flow5 ], [ true, %.Flow_crit_edge ]
  br i1 %33, label %loop.exit.guard1, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %20

34:                                               ; preds = %28
  %35 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %36 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 0, i1 true)
  %37 = call i1 @llvm.is.fpclass.f32(float %35, i32 144)
  %38 = call float @llvm.copysign.f32(float 0.000000e+00, float %35)
  %39 = select i1 %37, float %38, float %35
  %40 = call i1 @llvm.is.fpclass.f32(float %36, i32 144)
  %41 = call float @llvm.copysign.f32(float 0.000000e+00, float %36)
  %42 = select i1 %40, float %41, float %36
  %43 = fdiv float %39, %42
  %44 = call i1 @llvm.is.fpclass.f32(float %43, i32 144)
  %45 = call float @llvm.copysign.f32(float 0.000000e+00, float %43)
  %46 = select i1 %44, float %45, float %43
  %47 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %48 = fcmp uge float %46, %47
  br label %Flow4

Flow7:                                            ; preds = %loop.exit.guard1.Flow7_crit_edge, %22
  %49 = phi i32 [ %23, %22 ], [ poison, %loop.exit.guard1.Flow7_crit_edge ]
  %50 = phi i1 [ false, %22 ], [ true, %loop.exit.guard1.Flow7_crit_edge ]
  br label %Flow6

loop.exit.guard:                                  ; preds = %Flow6
  br i1 %54, label %7, label %loop.exit.guard.common.ret_crit_edge

loop.exit.guard.common.ret_crit_edge:             ; preds = %loop.exit.guard
  br label %common.ret

Flow5:                                            ; preds = %Flow4.Flow5_crit_edge, %26
  %51 = phi i32 [ %27, %26 ], [ poison, %Flow4.Flow5_crit_edge ]
  %52 = phi i1 [ false, %26 ], [ true, %Flow4.Flow5_crit_edge ]
  br label %Flow

Flow6:                                            ; preds = %.Flow6_crit_edge, %Flow7
  %53 = phi i32 [ %49, %Flow7 ], [ poison, %.Flow6_crit_edge ]
  %54 = phi i1 [ false, %Flow7 ], [ true, %.Flow6_crit_edge ]
  %55 = phi i1 [ %50, %Flow7 ], [ true, %.Flow6_crit_edge ]
  br i1 %55, label %loop.exit.guard, label %Flow6._crit_edge

Flow6._crit_edge:                                 ; preds = %Flow6
  br label %5

loop.exit.guard1:                                 ; preds = %Flow
  br i1 %32, label %22, label %loop.exit.guard1.Flow7_crit_edge

loop.exit.guard1.Flow7_crit_edge:                 ; preds = %loop.exit.guard1
  br label %Flow7
}

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
