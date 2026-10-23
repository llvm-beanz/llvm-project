; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap ENV6(c)(2)(p): a real, captured pre-`feme-cpu-linearize` IR
; reduction of `graphicsfuzz/cov-two-nested-loops-switch-case-matrix-
; array-increment` (the `ENV6(c)(2)(m)` hang investigation's own
; standalone repro, saved under `/tmp/env6c2m/` across several prior
; sessions), which also happens to exercise a second, independent,
; genuinely nondeterministic (ASLR-dependent) use-after-free bug in
; `LoopLinearizer::linearizeCycle` -- found via a live `gdb` repro of a
; standalone `feme-opt -passes=feme-cpu-linearize` loop over this exact
; module, roughly 1-in-10 to 1-in-30 iterations crashing in
; `llvm::Value::assertModuleIsMaterializedImpl()` (reached from
; `BasicBlock::hasNPredecessors`, called by `DiamondFlattener::validate`)
; on a dangling `BasicBlock *` -- confirmed, via safe pointer-identity-
; only tracing (comparing the suspect pointer's address against every
; block still present in the function, never dereferencing it), to be a
; `BasicBlock` already erased by `mergeTrivialRelayBlocksInCycle`.
;
; Root cause: `linearizeCycle`'s own `while (CollapsedRelay)` fixed-point
; loop runs, in sequence, `foldRedundantFlowBlocksInCycle`,
; `peelConstantFlowPredecessorsInCycle` (uses `DT`/`PDT`),
; `collapseTriviallyRedundantPhisInCycle`, then
; `mergeTrivialRelayBlocksInCycle` (which physically erases blocks via
; `MergeBlockIntoPredecessor`), looping back to the first two if either
; of the latter two changed anything -- but `DT`/`PDT` (passed by
; reference throughout the whole loop) were never recalculated between
; any of these steps. `peelConstantFlowPredecessorsInCycle`'s own
; internal helper, `peelWouldExposeSiblingDiamondToSharedMerge`, does
; its own *temporary* speculative `PDT.recalculate(F)` later on, but its
; very first scan (building its `ValidBeforePeel` set) uses the `PDT`
; exactly as handed to it by the caller, with no recalculate first. Once
; a prior iteration of the fixed-point loop had already erased a block
; via `mergeTrivialRelayBlocksInCycle`, that stale `PDT`'s node lookup
; for some unrelated candidate block could return a pointer to that
; now-freed `BasicBlock` as its "immediate post-dominator" -- passed
; straight into `DiamondFlattener::validate`, and ultimately dereferenced
; there, causing the crash.
;
; Fixed by adding `DT.recalculate(F); PDT.recalculate(F);` immediately
; before *both* calls to `peelConstantFlowPredecessorsInCycle` inside
; `linearizeCycle`'s fixed-point loop (the initial call, and the
; repeated call after `mergeTrivialRelayBlocksInCycle` may have erased
; blocks), matching this file's own established idiom of recalculating
; `DT`/`PDT` immediately before other dominance-sensitive calls
; elsewhere.
;
; This test does not assert on the crash itself (inherently
; nondeterministic, and this lit test runs without `gdb`/ASLR
; instrumentation, and in any case this module's own inner cycle shape
; is independently unsupported -- see `ENV6(c)(2)(o)`, still blocked on
; a *different*, not-yet-landed fix) -- it instead confirms the pass
; completes cleanly and emits exactly the expected `ENV6(c)(2)(o)`
; diagnostic (a pre-fix build could instead abort partway through, under
; the right allocator-reuse timing, well before ever reaching this
; diagnostic).
;
; CHECK: error: feme-cpu-linearize: function 'main': loop at '' has an internal branch in 'loop.exit.guard120' whose own check is not the sole entry into its 'stay in loop' continuation (e.g. a peeled predecessor bypassing the check directly); unsupported (roadmap milestone 6 deviation)

; ModuleID = '<in-memory object>'
source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

@spirv_var_16.str = private constant [13 x i8] c"spirv_var_16\00"
@spirv_var_40.str = private constant [13 x i8] c"spirv_var_40\00"
@_GLF_color = external addrspace(8) global <4 x float>, !spirv.Decorations !1
@gl_FragCoord = external addrspace(7) constant <4 x float>, !spirv.Decorations !3

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smin.i32(i32, i32) #0

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i1 @llvm.is.fpclass.f32(float, i32 immarg) #0

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.copysign.f32(float, float) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.spv.discard() #1

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smax.i32(i32, i32) #0

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.discard(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #2 !feme.signature !4 {
  %1 = alloca [2 x <4 x float>], align 4
  %2 = alloca [9 x float], align 4
  %3 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %4 = sitofp i32 %3 to float
  %5 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %6 = sitofp i32 %5 to float
  %7 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %8 = sitofp i32 %7 to float
  %9 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %10 = sitofp i32 %9 to float
  %11 = insertelement <4 x float> poison, float %4, i64 0
  %12 = insertelement <4 x float> %11, float %6, i64 1
  %13 = insertelement <4 x float> %12, float %8, i64 2
  %14 = insertelement <4 x float> %13, float %10, i64 3
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %4, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %6, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %8, i32 0)
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float %10, i32 0)
  %15 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %16 = insertelement <4 x float> <float poison, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>, float %15, i64 0
  %17 = insertelement <4 x float> <float 0.000000e+00, float poison, float 0.000000e+00, float 0.000000e+00>, float %15, i64 1
  store <4 x float> %16, ptr %1, align 4
  %.fca.1.gep33 = getelementptr inbounds nuw i8, ptr %1, i64 16
  store <4 x float> %17, ptr %.fca.1.gep33, align 4
  %18 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %19 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  br label %20

20:                                               ; preds = %Flow241._crit_edge, %0
  %.043 = phi i32 [ %19, %0 ], [ %1431, %Flow241._crit_edge ]
  %.035 = phi i32 [ %18, %0 ], [ %1430, %Flow241._crit_edge ]
  %21 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %22 = icmp slt i32 %.043, %21
  br i1 %22, label %._crit_edge, label %.Flow241_crit_edge

.Flow241_crit_edge:                               ; preds = %20
  br label %Flow241

._crit_edge:                                      ; preds = %20
  br label %45

Flow224:                                          ; preds = %loop.exit.guard.Flow224_crit_edge, %Flow126
  %23 = phi i1 [ false, %Flow126 ], [ true, %loop.exit.guard.Flow224_crit_edge ]
  br i1 %23, label %24, label %Flow224.Flow225_crit_edge

Flow224.Flow225_crit_edge:                        ; preds = %Flow224
  br label %Flow225

24:                                               ; preds = %Flow224
  %25 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %26 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 112, i1 true)
  %27 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 128, i1 true)
  %28 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 144, i1 true)
  %29 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %30 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i1 true)
  %31 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 48, i1 true)
  %32 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 64, i1 true)
  %33 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 80, i1 true)
  store float %25, ptr %2, align 4
  %.fca.1.gep = getelementptr inbounds nuw i8, ptr %2, i64 4
  store float %26, ptr %.fca.1.gep, align 4
  %.fca.2.gep = getelementptr inbounds nuw i8, ptr %2, i64 8
  store float %27, ptr %.fca.2.gep, align 4
  %.fca.3.gep = getelementptr inbounds nuw i8, ptr %2, i64 12
  store float %28, ptr %.fca.3.gep, align 4
  %.fca.4.gep = getelementptr inbounds nuw i8, ptr %2, i64 16
  store float %29, ptr %.fca.4.gep, align 4
  %.fca.5.gep = getelementptr inbounds nuw i8, ptr %2, i64 20
  store float %30, ptr %.fca.5.gep, align 4
  %.fca.6.gep = getelementptr inbounds nuw i8, ptr %2, i64 24
  store float %31, ptr %.fca.6.gep, align 4
  %.fca.7.gep = getelementptr inbounds nuw i8, ptr %2, i64 28
  store float %32, ptr %.fca.7.gep, align 4
  %.fca.8.gep = getelementptr inbounds nuw i8, ptr %2, i64 32
  store float %33, ptr %.fca.8.gep, align 4
  %34 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %35 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %36 = insertelement <4 x float> poison, float %35, i64 0
  %37 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %38 = insertelement <4 x float> %36, float %37, i64 1
  %39 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %40 = insertelement <4 x float> %38, float %39, i64 2
  %41 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %42 = insertelement <4 x float> %40, float %41, i64 3
  %43 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %44 = fcmp uge float %35, %43
  br i1 %44, label %1418, label %._crit_edge243

._crit_edge243:                                   ; preds = %24
  br label %1353

45:                                               ; preds = %Flow239._crit_edge, %._crit_edge
  %.042 = phi i32 [ %51, %Flow239._crit_edge ], [ 2, %._crit_edge ]
  %.1 = phi i32 [ %56, %Flow239._crit_edge ], [ %.035, %._crit_edge ]
  %46 = icmp slt i32 %.042, 5
  br i1 %46, label %49, label %.Flow239_crit_edge

.Flow239_crit_edge:                               ; preds = %45
  br label %Flow239

47:                                               ; preds = %loop.exit.guard116
  %48 = add i32 %.043, 1
  br label %Flow242

49:                                               ; preds = %45
  %50 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %58

Flow239:                                          ; preds = %.Flow239_crit_edge, %Flow240
  %51 = phi i32 [ %1427, %Flow240 ], [ poison, %.Flow239_crit_edge ]
  %52 = phi i1 [ false, %Flow240 ], [ true, %.Flow239_crit_edge ]
  %53 = phi i1 [ %1445, %Flow240 ], [ poison, %.Flow239_crit_edge ]
  %54 = phi i1 [ %1444, %Flow240 ], [ poison, %.Flow239_crit_edge ]
  %55 = phi i1 [ %1443, %Flow240 ], [ poison, %.Flow239_crit_edge ]
  %56 = phi i32 [ %1428, %Flow240 ], [ %.1, %.Flow239_crit_edge ]
  %57 = phi i1 [ %1429, %Flow240 ], [ true, %.Flow239_crit_edge ]
  br i1 %57, label %loop.exit.guard116, label %Flow239._crit_edge

Flow239._crit_edge:                               ; preds = %Flow239
  br label %45

58:                                               ; preds = %Flow237._crit_edge, %49
  %.041 = phi i32 [ %1441, %Flow237._crit_edge ], [ %50, %49 ]
  %59 = icmp slt i32 %.041, %.043
  br i1 %59, label %._crit_edge244, label %.Flow237_crit_edge

.Flow237_crit_edge:                               ; preds = %58
  br label %Flow237

._crit_edge244:                                   ; preds = %58
  br label %63

60:                                               ; preds = %loop.exit.guard118
  %61 = add i32 %.1, 1
  %62 = add i32 %.042, 1
  br label %Flow240

63:                                               ; preds = %Flow227._crit_edge, %._crit_edge244
  %.040 = phi i32 [ %68, %Flow227._crit_edge ], [ 0, %._crit_edge244 ]
  %64 = icmp slt i32 %.040, %.042
  br i1 %64, label %67, label %.Flow227_crit_edge

.Flow227_crit_edge:                               ; preds = %63
  br label %Flow227

65:                                               ; preds = %loop.exit.guard120
  %66 = add i32 %.041, 1
  br label %Flow238

67:                                               ; preds = %63
  br label %NodeBlock12

Flow227:                                          ; preds = %.Flow227_crit_edge, %Flow236
  %68 = phi i32 [ %1439, %Flow236 ], [ poison, %.Flow227_crit_edge ]
  %69 = phi i1 [ %96, %Flow236 ], [ true, %.Flow227_crit_edge ]
  %70 = phi i1 [ %97, %Flow236 ], [ poison, %.Flow227_crit_edge ]
  %71 = phi i1 [ %98, %Flow236 ], [ poison, %.Flow227_crit_edge ]
  %72 = phi i1 [ %99, %Flow236 ], [ poison, %.Flow227_crit_edge ]
  %73 = phi i1 [ %1440, %Flow236 ], [ true, %.Flow227_crit_edge ]
  br i1 %73, label %loop.exit.guard120, label %Flow227._crit_edge

Flow227._crit_edge:                               ; preds = %Flow227
  br label %63

NodeBlock12:                                      ; preds = %67
  %Pivot13 = icmp sge i32 %.1, 554
  br i1 %Pivot13, label %NodeBlock10, label %NodeBlock12.Flow228_crit_edge

NodeBlock12.Flow228_crit_edge:                    ; preds = %NodeBlock12
  br label %Flow228

NodeBlock10:                                      ; preds = %NodeBlock12
  %Pivot11 = icmp sge i32 %.1, 556
  br i1 %Pivot11, label %NodeBlock8, label %NodeBlock10.Flow229_crit_edge

NodeBlock10.Flow229_crit_edge:                    ; preds = %NodeBlock10
  br label %Flow229

Flow228:                                          ; preds = %NodeBlock12.Flow228_crit_edge, %Flow233
  %74 = phi i1 [ %91, %Flow233 ], [ true, %NodeBlock12.Flow228_crit_edge ]
  %75 = phi i1 [ %92, %Flow233 ], [ poison, %NodeBlock12.Flow228_crit_edge ]
  %76 = phi i1 [ %93, %Flow233 ], [ poison, %NodeBlock12.Flow228_crit_edge ]
  %77 = phi i1 [ %94, %Flow233 ], [ poison, %NodeBlock12.Flow228_crit_edge ]
  %78 = phi i1 [ %95, %Flow233 ], [ false, %NodeBlock12.Flow228_crit_edge ]
  %79 = phi i1 [ false, %Flow233 ], [ true, %NodeBlock12.Flow228_crit_edge ]
  br i1 %79, label %NodeBlock, label %Flow228.Flow234_crit_edge

Flow228.Flow234_crit_edge:                        ; preds = %Flow228
  br label %Flow234

NodeBlock8:                                       ; preds = %NodeBlock10
  %Pivot9 = icmp sge i32 %.1, 558
  br i1 %Pivot9, label %LeafBlock6, label %NodeBlock8.Flow230_crit_edge

NodeBlock8.Flow230_crit_edge:                     ; preds = %NodeBlock8
  br label %Flow230

Flow229:                                          ; preds = %NodeBlock10.Flow229_crit_edge, %Flow232
  %80 = phi i1 [ %87, %Flow232 ], [ true, %NodeBlock10.Flow229_crit_edge ]
  %81 = phi i1 [ %88, %Flow232 ], [ poison, %NodeBlock10.Flow229_crit_edge ]
  %82 = phi i1 [ %89, %Flow232 ], [ poison, %NodeBlock10.Flow229_crit_edge ]
  %83 = phi i1 [ %90, %Flow232 ], [ false, %NodeBlock10.Flow229_crit_edge ]
  %84 = phi i1 [ false, %Flow232 ], [ true, %NodeBlock10.Flow229_crit_edge ]
  br i1 %84, label %LeafBlock2, label %Flow229.Flow233_crit_edge

Flow229.Flow233_crit_edge:                        ; preds = %Flow229
  br label %Flow233

LeafBlock6:                                       ; preds = %NodeBlock8
  %SwitchLeaf7 = icmp eq i32 %.1, 558
  br i1 %SwitchLeaf7, label %120, label %LeafBlock6.Flow231_crit_edge

LeafBlock6.Flow231_crit_edge:                     ; preds = %LeafBlock6
  br label %Flow231

Flow231:                                          ; preds = %LeafBlock6.Flow231_crit_edge, %120
  %85 = phi i1 [ false, %120 ], [ true, %LeafBlock6.Flow231_crit_edge ]
  %86 = phi i1 [ %130, %120 ], [ true, %LeafBlock6.Flow231_crit_edge ]
  br label %Flow230

LeafBlock4:                                       ; preds = %Flow230
  %SwitchLeaf5 = icmp ne i32 %.1, 556
  br label %Flow232

Flow232:                                          ; preds = %Flow230.Flow232_crit_edge, %LeafBlock4
  %87 = phi i1 [ false, %LeafBlock4 ], [ %117, %Flow230.Flow232_crit_edge ]
  %88 = phi i1 [ false, %LeafBlock4 ], [ true, %Flow230.Flow232_crit_edge ]
  %89 = phi i1 [ true, %LeafBlock4 ], [ false, %Flow230.Flow232_crit_edge ]
  %90 = phi i1 [ %SwitchLeaf5, %LeafBlock4 ], [ %118, %Flow230.Flow232_crit_edge ]
  br label %Flow229

LeafBlock2:                                       ; preds = %Flow229
  %SwitchLeaf3 = icmp ne i32 %.1, 554
  br label %Flow233

Flow233:                                          ; preds = %Flow229.Flow233_crit_edge, %LeafBlock2
  %91 = phi i1 [ false, %LeafBlock2 ], [ %80, %Flow229.Flow233_crit_edge ]
  %92 = phi i1 [ false, %LeafBlock2 ], [ %81, %Flow229.Flow233_crit_edge ]
  %93 = phi i1 [ false, %LeafBlock2 ], [ %82, %Flow229.Flow233_crit_edge ]
  %94 = phi i1 [ true, %LeafBlock2 ], [ false, %Flow229.Flow233_crit_edge ]
  %95 = phi i1 [ %SwitchLeaf3, %LeafBlock2 ], [ %83, %Flow229.Flow233_crit_edge ]
  br label %Flow228

NodeBlock:                                        ; preds = %Flow228
  %Pivot = icmp slt i32 %.1, 553
  br i1 %Pivot, label %LeafBlock, label %NodeBlock.Flow235_crit_edge

NodeBlock.Flow235_crit_edge:                      ; preds = %NodeBlock
  br label %Flow235

Flow234:                                          ; preds = %Flow228.Flow234_crit_edge, %Flow235
  %96 = phi i1 [ false, %Flow235 ], [ %74, %Flow228.Flow234_crit_edge ]
  %97 = phi i1 [ false, %Flow235 ], [ %75, %Flow228.Flow234_crit_edge ]
  %98 = phi i1 [ false, %Flow235 ], [ %76, %Flow228.Flow234_crit_edge ]
  %99 = phi i1 [ false, %Flow235 ], [ %77, %Flow228.Flow234_crit_edge ]
  %100 = phi i1 [ %101, %Flow235 ], [ %78, %Flow228.Flow234_crit_edge ]
  br i1 %100, label %102, label %Flow234.Flow236_crit_edge

Flow234.Flow236_crit_edge:                        ; preds = %Flow234
  br label %Flow236

LeafBlock:                                        ; preds = %NodeBlock
  %SwitchLeaf = icmp eq i32 %.1, 2
  br i1 %SwitchLeaf, label %104, label %LeafBlock.Flow226_crit_edge

LeafBlock.Flow226_crit_edge:                      ; preds = %LeafBlock
  br label %Flow226

Flow226:                                          ; preds = %LeafBlock.Flow226_crit_edge, %104
  br label %Flow235

Flow235:                                          ; preds = %NodeBlock.Flow235_crit_edge, %Flow226
  %101 = phi i1 [ true, %Flow226 ], [ %78, %NodeBlock.Flow235_crit_edge ]
  br label %Flow234

102:                                              ; preds = %Flow234
  %103 = add i32 %.040, 1
  br label %Flow236

104:                                              ; preds = %LeafBlock
  %105 = icmp sgt i32 %.041, 0
  %106 = call i32 @llvm.smax.i32(i32 %.040, i32 0)
  %107 = call i32 @llvm.smin.i32(i32 %106, i32 3)
  %108 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %109 = zext i1 %105 to i64
  %110 = zext nneg i32 %107 to i64
  %.sroa.gep = getelementptr [16 x i8], ptr %1, i64 1
  %.sroa.gep1 = getelementptr [16 x i8], ptr %1, i64 0
  %.sroa.sel = select i1 %105, ptr %.sroa.gep, ptr %.sroa.gep1
  %111 = getelementptr [4 x i8], ptr %.sroa.sel, i64 %110
  store float %108, ptr %111, align 4
  br label %Flow226

Flow122:                                          ; preds = %Flow.Flow122_crit_edge, %114
  br label %Flow123

Flow124:                                          ; preds = %Flow123.Flow124_crit_edge, %116
  br label %Flow125

Flow126:                                          ; preds = %Flow125.Flow126_crit_edge, %132
  br label %Flow224

common.ret:                                       ; preds = %Flow225
  ret void

112:                                              ; preds = %loop.exit.guard112
  call void @feme.stage.discard(i1 true)
  br label %Flow

Flow:                                             ; preds = %loop.exit.guard112.Flow_crit_edge, %112
  %113 = phi i1 [ false, %112 ], [ true, %loop.exit.guard112.Flow_crit_edge ]
  br i1 %113, label %114, label %Flow.Flow122_crit_edge

Flow.Flow122_crit_edge:                           ; preds = %Flow
  br label %Flow122

114:                                              ; preds = %Flow
  call void @feme.stage.discard(i1 true)
  br label %Flow122

Flow123:                                          ; preds = %loop.exit.guard111.Flow123_crit_edge, %Flow122
  %115 = phi i1 [ false, %Flow122 ], [ true, %loop.exit.guard111.Flow123_crit_edge ]
  br i1 %115, label %116, label %Flow123.Flow124_crit_edge

Flow123.Flow124_crit_edge:                        ; preds = %Flow123
  br label %Flow124

116:                                              ; preds = %Flow123
  call void @feme.stage.discard(i1 true)
  br label %Flow124

Flow230:                                          ; preds = %NodeBlock8.Flow230_crit_edge, %Flow231
  %117 = phi i1 [ %85, %Flow231 ], [ true, %NodeBlock8.Flow230_crit_edge ]
  %118 = phi i1 [ %86, %Flow231 ], [ false, %NodeBlock8.Flow230_crit_edge ]
  %119 = phi i1 [ false, %Flow231 ], [ true, %NodeBlock8.Flow230_crit_edge ]
  br i1 %119, label %LeafBlock4, label %Flow230.Flow232_crit_edge

Flow230.Flow232_crit_edge:                        ; preds = %Flow230
  br label %Flow232

120:                                              ; preds = %LeafBlock6
  %121 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %122 = insertelement <4 x float> poison, float %121, i64 0
  %123 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %124 = insertelement <4 x float> %122, float %123, i64 1
  %125 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %126 = insertelement <4 x float> %124, float %125, i64 2
  %127 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %128 = insertelement <4 x float> %126, float %127, i64 3
  %129 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %130 = fcmp uge float %123, %129
  br label %Flow231

Flow125:                                          ; preds = %loop.exit.guard110.Flow125_crit_edge, %Flow124
  %131 = phi i1 [ false, %Flow124 ], [ true, %loop.exit.guard110.Flow125_crit_edge ]
  br i1 %131, label %132, label %Flow125.Flow126_crit_edge

Flow125.Flow126_crit_edge:                        ; preds = %Flow125
  br label %Flow126

132:                                              ; preds = %Flow125
  call void @feme.stage.discard(i1 true)
  br label %Flow126

133:                                              ; preds = %1407
  %134 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %135 = call i32 @llvm.smin.i32(i32 %.2.3, i32 %134)
  %136 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %137 = sext i32 %135 to i64
  %138 = getelementptr [4 x i8], ptr %2, i64 %137
  store float %136, ptr %138, align 4
  br label %140

Flow223:                                          ; preds = %.Flow223_crit_edge, %142
  %139 = phi i32 [ %143, %142 ], [ %.2.3, %.Flow223_crit_edge ]
  br label %199

140:                                              ; preds = %Flow222._crit_edge, %133
  %.034.149 = phi i32 [ %146, %Flow222._crit_edge ], [ 0, %133 ]
  %141 = icmp slt i32 %.034.149, 1
  br i1 %141, label %144, label %.Flow222_crit_edge

.Flow222_crit_edge:                               ; preds = %140
  br label %Flow222

142:                                              ; preds = %Flow222
  %143 = add i32 %.2.3, 1
  br label %Flow223

144:                                              ; preds = %140
  %145 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %148

Flow222:                                          ; preds = %.Flow222_crit_edge, %150
  %146 = phi i32 [ %151, %150 ], [ poison, %.Flow222_crit_edge ]
  %147 = phi i1 [ false, %150 ], [ true, %.Flow222_crit_edge ]
  br i1 %147, label %142, label %Flow222._crit_edge

Flow222._crit_edge:                               ; preds = %Flow222
  br label %140

148:                                              ; preds = %Flow221._crit_edge, %144
  %.0.150 = phi i32 [ %155, %Flow221._crit_edge ], [ %145, %144 ]
  %149 = icmp slt i32 %.0.150, 1
  br i1 %149, label %152, label %.Flow221_crit_edge

.Flow221_crit_edge:                               ; preds = %148
  br label %Flow221

150:                                              ; preds = %Flow221
  %151 = add i32 %.034.149, 1
  br label %Flow222

152:                                              ; preds = %148
  %153 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %154 = sub i32 %135, %153
  br label %NodeBlock18

Flow221:                                          ; preds = %.Flow221_crit_edge, %197
  %155 = phi i32 [ %198, %197 ], [ poison, %.Flow221_crit_edge ]
  %156 = phi i1 [ false, %197 ], [ true, %.Flow221_crit_edge ]
  br i1 %156, label %150, label %Flow221._crit_edge

Flow221._crit_edge:                               ; preds = %Flow221
  br label %148

NodeBlock18:                                      ; preds = %152
  %Pivot19 = icmp sge i32 %154, 2
  br i1 %Pivot19, label %LeafBlock16, label %NodeBlock18.Flow220_crit_edge

NodeBlock18.Flow220_crit_edge:                    ; preds = %NodeBlock18
  br label %Flow220

LeafBlock16:                                      ; preds = %NodeBlock18
  %SwitchLeaf17 = icmp eq i32 %154, 2
  br i1 %SwitchLeaf17, label %158, label %LeafBlock16.Flow218_crit_edge

LeafBlock16.Flow218_crit_edge:                    ; preds = %LeafBlock16
  br label %Flow218

Flow220:                                          ; preds = %NodeBlock18.Flow220_crit_edge, %Flow218
  %157 = phi i1 [ false, %Flow218 ], [ true, %NodeBlock18.Flow220_crit_edge ]
  br i1 %157, label %LeafBlock14, label %Flow220._crit_edge

Flow220._crit_edge:                               ; preds = %Flow220
  br label %197

LeafBlock14:                                      ; preds = %Flow220
  %SwitchLeaf15 = icmp eq i32 %154, 0
  br i1 %SwitchLeaf15, label %180, label %LeafBlock14.Flow219_crit_edge

LeafBlock14.Flow219_crit_edge:                    ; preds = %LeafBlock14
  br label %Flow219

158:                                              ; preds = %LeafBlock16
  %159 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %160 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %161 = sext i32 %159 to i64
  %162 = sext i32 %160 to i64
  %.split = getelementptr [16 x i8], ptr %1, i64 %161
  %163 = getelementptr [4 x i8], ptr %.split, i64 %162
  %164 = load float, ptr %163, align 4
  %165 = sext i32 %135 to i64
  %166 = getelementptr [4 x i8], ptr %2, i64 %165
  %167 = load float, ptr %166, align 4
  %168 = call i1 @llvm.is.fpclass.f32(float %167, /* (sub) */ i32 144)
  %169 = call float @llvm.copysign.f32(float 0.000000e+00, float %167)
  %170 = select i1 %168, float %169, float %167
  %171 = call i1 @llvm.is.fpclass.f32(float %164, /* (sub) */ i32 144)
  %172 = call float @llvm.copysign.f32(float 0.000000e+00, float %164)
  %173 = select i1 %171, float %172, float %164
  %174 = fadd float %170, %173
  %175 = call i1 @llvm.is.fpclass.f32(float %174, /* (sub) */ i32 144)
  %176 = call float @llvm.copysign.f32(float 0.000000e+00, float %174)
  %177 = select i1 %175, float %176, float %174
  %178 = sext i32 %135 to i64
  %179 = getelementptr [4 x i8], ptr %2, i64 %178
  store float %177, ptr %179, align 4
  br label %Flow218

180:                                              ; preds = %LeafBlock14
  %181 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %182 = sext i32 %135 to i64
  %183 = getelementptr [4 x i8], ptr %2, i64 %182
  %184 = load float, ptr %183, align 4
  %185 = call i1 @llvm.is.fpclass.f32(float %184, /* (sub) */ i32 144)
  %186 = call float @llvm.copysign.f32(float 0.000000e+00, float %184)
  %187 = select i1 %185, float %186, float %184
  %188 = call i1 @llvm.is.fpclass.f32(float %181, /* (sub) */ i32 144)
  %189 = call float @llvm.copysign.f32(float 0.000000e+00, float %181)
  %190 = select i1 %188, float %189, float %181
  %191 = fadd float %187, %190
  %192 = call i1 @llvm.is.fpclass.f32(float %191, /* (sub) */ i32 144)
  %193 = call float @llvm.copysign.f32(float 0.000000e+00, float %191)
  %194 = select i1 %192, float %193, float %191
  %195 = sext i32 %135 to i64
  %196 = getelementptr [4 x i8], ptr %2, i64 %195
  store float %194, ptr %196, align 4
  br label %Flow219

Flow218:                                          ; preds = %LeafBlock16.Flow218_crit_edge, %158
  br label %Flow220

Flow219:                                          ; preds = %LeafBlock14.Flow219_crit_edge, %180
  br label %197

197:                                              ; preds = %Flow220._crit_edge, %Flow219
  %198 = add i32 %.0.150, 1
  br label %Flow221

199:                                              ; preds = %Flow223
  %200 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %201 = insertelement <4 x float> poison, float %200, i64 0
  %202 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %203 = insertelement <4 x float> %201, float %202, i64 1
  %204 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %205 = insertelement <4 x float> %203, float %204, i64 2
  %206 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %207 = insertelement <4 x float> %205, float %206, i64 3
  %208 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %209 = fcmp uge float %200, %208
  br i1 %209, label %210, label %.Flow217_crit_edge

.Flow217_crit_edge:                               ; preds = %199
  br label %Flow217

210:                                              ; preds = %199
  %211 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %212 = call i32 @llvm.smin.i32(i32 %139, i32 %211)
  %213 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %214 = sext i32 %212 to i64
  %215 = getelementptr [4 x i8], ptr %2, i64 %214
  store float %213, ptr %215, align 4
  br label %217

Flow217:                                          ; preds = %.Flow217_crit_edge, %219
  %216 = phi i32 [ %220, %219 ], [ %139, %.Flow217_crit_edge ]
  br label %276

217:                                              ; preds = %Flow216._crit_edge, %210
  %.034.1.1 = phi i32 [ %223, %Flow216._crit_edge ], [ 0, %210 ]
  %218 = icmp slt i32 %.034.1.1, 1
  br i1 %218, label %221, label %.Flow216_crit_edge

.Flow216_crit_edge:                               ; preds = %217
  br label %Flow216

219:                                              ; preds = %Flow216
  %220 = add i32 %139, 1
  br label %Flow217

221:                                              ; preds = %217
  %222 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %225

Flow216:                                          ; preds = %.Flow216_crit_edge, %227
  %223 = phi i32 [ %228, %227 ], [ poison, %.Flow216_crit_edge ]
  %224 = phi i1 [ false, %227 ], [ true, %.Flow216_crit_edge ]
  br i1 %224, label %219, label %Flow216._crit_edge

Flow216._crit_edge:                               ; preds = %Flow216
  br label %217

225:                                              ; preds = %Flow215._crit_edge, %221
  %.0.1.1 = phi i32 [ %232, %Flow215._crit_edge ], [ %222, %221 ]
  %226 = icmp slt i32 %.0.1.1, 2
  br i1 %226, label %229, label %.Flow215_crit_edge

.Flow215_crit_edge:                               ; preds = %225
  br label %Flow215

227:                                              ; preds = %Flow215
  %228 = add i32 %.034.1.1, 1
  br label %Flow216

229:                                              ; preds = %225
  %230 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %231 = sub i32 %212, %230
  br label %NodeBlock24

Flow215:                                          ; preds = %.Flow215_crit_edge, %274
  %232 = phi i32 [ %275, %274 ], [ poison, %.Flow215_crit_edge ]
  %233 = phi i1 [ false, %274 ], [ true, %.Flow215_crit_edge ]
  br i1 %233, label %227, label %Flow215._crit_edge

Flow215._crit_edge:                               ; preds = %Flow215
  br label %225

NodeBlock24:                                      ; preds = %229
  %Pivot25 = icmp sge i32 %231, 2
  br i1 %Pivot25, label %LeafBlock22, label %NodeBlock24.Flow214_crit_edge

NodeBlock24.Flow214_crit_edge:                    ; preds = %NodeBlock24
  br label %Flow214

LeafBlock22:                                      ; preds = %NodeBlock24
  %SwitchLeaf23 = icmp eq i32 %231, 2
  br i1 %SwitchLeaf23, label %235, label %LeafBlock22.Flow212_crit_edge

LeafBlock22.Flow212_crit_edge:                    ; preds = %LeafBlock22
  br label %Flow212

Flow214:                                          ; preds = %NodeBlock24.Flow214_crit_edge, %Flow212
  %234 = phi i1 [ false, %Flow212 ], [ true, %NodeBlock24.Flow214_crit_edge ]
  br i1 %234, label %LeafBlock20, label %Flow214._crit_edge

Flow214._crit_edge:                               ; preds = %Flow214
  br label %274

LeafBlock20:                                      ; preds = %Flow214
  %SwitchLeaf21 = icmp eq i32 %231, 0
  br i1 %SwitchLeaf21, label %257, label %LeafBlock20.Flow213_crit_edge

LeafBlock20.Flow213_crit_edge:                    ; preds = %LeafBlock20
  br label %Flow213

235:                                              ; preds = %LeafBlock22
  %236 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %237 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %238 = sext i32 %236 to i64
  %239 = sext i32 %237 to i64
  %.split60 = getelementptr [16 x i8], ptr %1, i64 %238
  %240 = getelementptr [4 x i8], ptr %.split60, i64 %239
  %241 = load float, ptr %240, align 4
  %242 = sext i32 %212 to i64
  %243 = getelementptr [4 x i8], ptr %2, i64 %242
  %244 = load float, ptr %243, align 4
  %245 = call i1 @llvm.is.fpclass.f32(float %244, /* (sub) */ i32 144)
  %246 = call float @llvm.copysign.f32(float 0.000000e+00, float %244)
  %247 = select i1 %245, float %246, float %244
  %248 = call i1 @llvm.is.fpclass.f32(float %241, /* (sub) */ i32 144)
  %249 = call float @llvm.copysign.f32(float 0.000000e+00, float %241)
  %250 = select i1 %248, float %249, float %241
  %251 = fadd float %247, %250
  %252 = call i1 @llvm.is.fpclass.f32(float %251, /* (sub) */ i32 144)
  %253 = call float @llvm.copysign.f32(float 0.000000e+00, float %251)
  %254 = select i1 %252, float %253, float %251
  %255 = sext i32 %212 to i64
  %256 = getelementptr [4 x i8], ptr %2, i64 %255
  store float %254, ptr %256, align 4
  br label %Flow212

257:                                              ; preds = %LeafBlock20
  %258 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %259 = sext i32 %212 to i64
  %260 = getelementptr [4 x i8], ptr %2, i64 %259
  %261 = load float, ptr %260, align 4
  %262 = call i1 @llvm.is.fpclass.f32(float %261, /* (sub) */ i32 144)
  %263 = call float @llvm.copysign.f32(float 0.000000e+00, float %261)
  %264 = select i1 %262, float %263, float %261
  %265 = call i1 @llvm.is.fpclass.f32(float %258, /* (sub) */ i32 144)
  %266 = call float @llvm.copysign.f32(float 0.000000e+00, float %258)
  %267 = select i1 %265, float %266, float %258
  %268 = fadd float %264, %267
  %269 = call i1 @llvm.is.fpclass.f32(float %268, /* (sub) */ i32 144)
  %270 = call float @llvm.copysign.f32(float 0.000000e+00, float %268)
  %271 = select i1 %269, float %270, float %268
  %272 = sext i32 %212 to i64
  %273 = getelementptr [4 x i8], ptr %2, i64 %272
  store float %271, ptr %273, align 4
  br label %Flow213

Flow212:                                          ; preds = %LeafBlock22.Flow212_crit_edge, %235
  br label %Flow214

Flow213:                                          ; preds = %LeafBlock20.Flow213_crit_edge, %257
  br label %274

274:                                              ; preds = %Flow214._crit_edge, %Flow213
  %275 = add i32 %.0.1.1, 1
  br label %Flow215

276:                                              ; preds = %Flow217
  %277 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %278 = insertelement <4 x float> poison, float %277, i64 0
  %279 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %280 = insertelement <4 x float> %278, float %279, i64 1
  %281 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %282 = insertelement <4 x float> %280, float %281, i64 2
  %283 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %284 = insertelement <4 x float> %282, float %283, i64 3
  %285 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %286 = fcmp uge float %277, %285
  br i1 %286, label %287, label %.Flow211_crit_edge

.Flow211_crit_edge:                               ; preds = %276
  br label %Flow211

287:                                              ; preds = %276
  %288 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %289 = call i32 @llvm.smin.i32(i32 %216, i32 %288)
  %290 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %291 = sext i32 %289 to i64
  %292 = getelementptr [4 x i8], ptr %2, i64 %291
  store float %290, ptr %292, align 4
  br label %294

Flow211:                                          ; preds = %.Flow211_crit_edge, %296
  %293 = phi i32 [ %297, %296 ], [ %216, %.Flow211_crit_edge ]
  br label %353

294:                                              ; preds = %Flow210._crit_edge, %287
  %.034.2.1 = phi i32 [ %300, %Flow210._crit_edge ], [ 0, %287 ]
  %295 = icmp slt i32 %.034.2.1, 1
  br i1 %295, label %298, label %.Flow210_crit_edge

.Flow210_crit_edge:                               ; preds = %294
  br label %Flow210

296:                                              ; preds = %Flow210
  %297 = add i32 %216, 1
  br label %Flow211

298:                                              ; preds = %294
  %299 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %302

Flow210:                                          ; preds = %.Flow210_crit_edge, %304
  %300 = phi i32 [ %305, %304 ], [ poison, %.Flow210_crit_edge ]
  %301 = phi i1 [ false, %304 ], [ true, %.Flow210_crit_edge ]
  br i1 %301, label %296, label %Flow210._crit_edge

Flow210._crit_edge:                               ; preds = %Flow210
  br label %294

302:                                              ; preds = %Flow209._crit_edge, %298
  %.0.2.1 = phi i32 [ %309, %Flow209._crit_edge ], [ %299, %298 ]
  %303 = icmp slt i32 %.0.2.1, 3
  br i1 %303, label %306, label %.Flow209_crit_edge

.Flow209_crit_edge:                               ; preds = %302
  br label %Flow209

304:                                              ; preds = %Flow209
  %305 = add i32 %.034.2.1, 1
  br label %Flow210

306:                                              ; preds = %302
  %307 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %308 = sub i32 %289, %307
  br label %NodeBlock30

Flow209:                                          ; preds = %.Flow209_crit_edge, %351
  %309 = phi i32 [ %352, %351 ], [ poison, %.Flow209_crit_edge ]
  %310 = phi i1 [ false, %351 ], [ true, %.Flow209_crit_edge ]
  br i1 %310, label %304, label %Flow209._crit_edge

Flow209._crit_edge:                               ; preds = %Flow209
  br label %302

NodeBlock30:                                      ; preds = %306
  %Pivot31 = icmp sge i32 %308, 2
  br i1 %Pivot31, label %LeafBlock28, label %NodeBlock30.Flow208_crit_edge

NodeBlock30.Flow208_crit_edge:                    ; preds = %NodeBlock30
  br label %Flow208

LeafBlock28:                                      ; preds = %NodeBlock30
  %SwitchLeaf29 = icmp eq i32 %308, 2
  br i1 %SwitchLeaf29, label %312, label %LeafBlock28.Flow206_crit_edge

LeafBlock28.Flow206_crit_edge:                    ; preds = %LeafBlock28
  br label %Flow206

Flow208:                                          ; preds = %NodeBlock30.Flow208_crit_edge, %Flow206
  %311 = phi i1 [ false, %Flow206 ], [ true, %NodeBlock30.Flow208_crit_edge ]
  br i1 %311, label %LeafBlock26, label %Flow208._crit_edge

Flow208._crit_edge:                               ; preds = %Flow208
  br label %351

LeafBlock26:                                      ; preds = %Flow208
  %SwitchLeaf27 = icmp eq i32 %308, 0
  br i1 %SwitchLeaf27, label %334, label %LeafBlock26.Flow207_crit_edge

LeafBlock26.Flow207_crit_edge:                    ; preds = %LeafBlock26
  br label %Flow207

312:                                              ; preds = %LeafBlock28
  %313 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %314 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %315 = sext i32 %313 to i64
  %316 = sext i32 %314 to i64
  %.split61 = getelementptr [16 x i8], ptr %1, i64 %315
  %317 = getelementptr [4 x i8], ptr %.split61, i64 %316
  %318 = load float, ptr %317, align 4
  %319 = sext i32 %289 to i64
  %320 = getelementptr [4 x i8], ptr %2, i64 %319
  %321 = load float, ptr %320, align 4
  %322 = call i1 @llvm.is.fpclass.f32(float %321, /* (sub) */ i32 144)
  %323 = call float @llvm.copysign.f32(float 0.000000e+00, float %321)
  %324 = select i1 %322, float %323, float %321
  %325 = call i1 @llvm.is.fpclass.f32(float %318, /* (sub) */ i32 144)
  %326 = call float @llvm.copysign.f32(float 0.000000e+00, float %318)
  %327 = select i1 %325, float %326, float %318
  %328 = fadd float %324, %327
  %329 = call i1 @llvm.is.fpclass.f32(float %328, /* (sub) */ i32 144)
  %330 = call float @llvm.copysign.f32(float 0.000000e+00, float %328)
  %331 = select i1 %329, float %330, float %328
  %332 = sext i32 %289 to i64
  %333 = getelementptr [4 x i8], ptr %2, i64 %332
  store float %331, ptr %333, align 4
  br label %Flow206

334:                                              ; preds = %LeafBlock26
  %335 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %336 = sext i32 %289 to i64
  %337 = getelementptr [4 x i8], ptr %2, i64 %336
  %338 = load float, ptr %337, align 4
  %339 = call i1 @llvm.is.fpclass.f32(float %338, /* (sub) */ i32 144)
  %340 = call float @llvm.copysign.f32(float 0.000000e+00, float %338)
  %341 = select i1 %339, float %340, float %338
  %342 = call i1 @llvm.is.fpclass.f32(float %335, /* (sub) */ i32 144)
  %343 = call float @llvm.copysign.f32(float 0.000000e+00, float %335)
  %344 = select i1 %342, float %343, float %335
  %345 = fadd float %341, %344
  %346 = call i1 @llvm.is.fpclass.f32(float %345, /* (sub) */ i32 144)
  %347 = call float @llvm.copysign.f32(float 0.000000e+00, float %345)
  %348 = select i1 %346, float %347, float %345
  %349 = sext i32 %289 to i64
  %350 = getelementptr [4 x i8], ptr %2, i64 %349
  store float %348, ptr %350, align 4
  br label %Flow207

Flow206:                                          ; preds = %LeafBlock28.Flow206_crit_edge, %312
  br label %Flow208

Flow207:                                          ; preds = %LeafBlock26.Flow207_crit_edge, %334
  br label %351

351:                                              ; preds = %Flow208._crit_edge, %Flow207
  %352 = add i32 %.0.2.1, 1
  br label %Flow209

353:                                              ; preds = %Flow211
  %354 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %355 = insertelement <4 x float> poison, float %354, i64 0
  %356 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %357 = insertelement <4 x float> %355, float %356, i64 1
  %358 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %359 = insertelement <4 x float> %357, float %358, i64 2
  %360 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %361 = insertelement <4 x float> %359, float %360, i64 3
  %362 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %363 = fcmp uge float %354, %362
  br i1 %363, label %364, label %.Flow205_crit_edge

.Flow205_crit_edge:                               ; preds = %353
  br label %Flow205

364:                                              ; preds = %353
  %365 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %366 = call i32 @llvm.smin.i32(i32 %293, i32 %365)
  %367 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %368 = sext i32 %366 to i64
  %369 = getelementptr [4 x i8], ptr %2, i64 %368
  store float %367, ptr %369, align 4
  br label %371

Flow205:                                          ; preds = %.Flow205_crit_edge, %373
  %370 = phi i32 [ %374, %373 ], [ %293, %.Flow205_crit_edge ]
  br label %430

371:                                              ; preds = %Flow204._crit_edge, %364
  %.034.3.1 = phi i32 [ %377, %Flow204._crit_edge ], [ 0, %364 ]
  %372 = icmp slt i32 %.034.3.1, 1
  br i1 %372, label %375, label %.Flow204_crit_edge

.Flow204_crit_edge:                               ; preds = %371
  br label %Flow204

373:                                              ; preds = %Flow204
  %374 = add i32 %293, 1
  br label %Flow205

375:                                              ; preds = %371
  %376 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %379

Flow204:                                          ; preds = %.Flow204_crit_edge, %381
  %377 = phi i32 [ %382, %381 ], [ poison, %.Flow204_crit_edge ]
  %378 = phi i1 [ false, %381 ], [ true, %.Flow204_crit_edge ]
  br i1 %378, label %373, label %Flow204._crit_edge

Flow204._crit_edge:                               ; preds = %Flow204
  br label %371

379:                                              ; preds = %Flow203._crit_edge, %375
  %.0.3.1 = phi i32 [ %386, %Flow203._crit_edge ], [ %376, %375 ]
  %380 = icmp slt i32 %.0.3.1, 4
  br i1 %380, label %383, label %.Flow203_crit_edge

.Flow203_crit_edge:                               ; preds = %379
  br label %Flow203

381:                                              ; preds = %Flow203
  %382 = add i32 %.034.3.1, 1
  br label %Flow204

383:                                              ; preds = %379
  %384 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %385 = sub i32 %366, %384
  br label %NodeBlock36

Flow203:                                          ; preds = %.Flow203_crit_edge, %428
  %386 = phi i32 [ %429, %428 ], [ poison, %.Flow203_crit_edge ]
  %387 = phi i1 [ false, %428 ], [ true, %.Flow203_crit_edge ]
  br i1 %387, label %381, label %Flow203._crit_edge

Flow203._crit_edge:                               ; preds = %Flow203
  br label %379

NodeBlock36:                                      ; preds = %383
  %Pivot37 = icmp sge i32 %385, 2
  br i1 %Pivot37, label %LeafBlock34, label %NodeBlock36.Flow202_crit_edge

NodeBlock36.Flow202_crit_edge:                    ; preds = %NodeBlock36
  br label %Flow202

LeafBlock34:                                      ; preds = %NodeBlock36
  %SwitchLeaf35 = icmp eq i32 %385, 2
  br i1 %SwitchLeaf35, label %389, label %LeafBlock34.Flow200_crit_edge

LeafBlock34.Flow200_crit_edge:                    ; preds = %LeafBlock34
  br label %Flow200

Flow202:                                          ; preds = %NodeBlock36.Flow202_crit_edge, %Flow200
  %388 = phi i1 [ false, %Flow200 ], [ true, %NodeBlock36.Flow202_crit_edge ]
  br i1 %388, label %LeafBlock32, label %Flow202._crit_edge

Flow202._crit_edge:                               ; preds = %Flow202
  br label %428

LeafBlock32:                                      ; preds = %Flow202
  %SwitchLeaf33 = icmp eq i32 %385, 0
  br i1 %SwitchLeaf33, label %411, label %LeafBlock32.Flow201_crit_edge

LeafBlock32.Flow201_crit_edge:                    ; preds = %LeafBlock32
  br label %Flow201

389:                                              ; preds = %LeafBlock34
  %390 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %391 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %392 = sext i32 %390 to i64
  %393 = sext i32 %391 to i64
  %.split62 = getelementptr [16 x i8], ptr %1, i64 %392
  %394 = getelementptr [4 x i8], ptr %.split62, i64 %393
  %395 = load float, ptr %394, align 4
  %396 = sext i32 %366 to i64
  %397 = getelementptr [4 x i8], ptr %2, i64 %396
  %398 = load float, ptr %397, align 4
  %399 = call i1 @llvm.is.fpclass.f32(float %398, /* (sub) */ i32 144)
  %400 = call float @llvm.copysign.f32(float 0.000000e+00, float %398)
  %401 = select i1 %399, float %400, float %398
  %402 = call i1 @llvm.is.fpclass.f32(float %395, /* (sub) */ i32 144)
  %403 = call float @llvm.copysign.f32(float 0.000000e+00, float %395)
  %404 = select i1 %402, float %403, float %395
  %405 = fadd float %401, %404
  %406 = call i1 @llvm.is.fpclass.f32(float %405, /* (sub) */ i32 144)
  %407 = call float @llvm.copysign.f32(float 0.000000e+00, float %405)
  %408 = select i1 %406, float %407, float %405
  %409 = sext i32 %366 to i64
  %410 = getelementptr [4 x i8], ptr %2, i64 %409
  store float %408, ptr %410, align 4
  br label %Flow200

411:                                              ; preds = %LeafBlock32
  %412 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %413 = sext i32 %366 to i64
  %414 = getelementptr [4 x i8], ptr %2, i64 %413
  %415 = load float, ptr %414, align 4
  %416 = call i1 @llvm.is.fpclass.f32(float %415, /* (sub) */ i32 144)
  %417 = call float @llvm.copysign.f32(float 0.000000e+00, float %415)
  %418 = select i1 %416, float %417, float %415
  %419 = call i1 @llvm.is.fpclass.f32(float %412, /* (sub) */ i32 144)
  %420 = call float @llvm.copysign.f32(float 0.000000e+00, float %412)
  %421 = select i1 %419, float %420, float %412
  %422 = fadd float %418, %421
  %423 = call i1 @llvm.is.fpclass.f32(float %422, /* (sub) */ i32 144)
  %424 = call float @llvm.copysign.f32(float 0.000000e+00, float %422)
  %425 = select i1 %423, float %424, float %422
  %426 = sext i32 %366 to i64
  %427 = getelementptr [4 x i8], ptr %2, i64 %426
  store float %425, ptr %427, align 4
  br label %Flow201

Flow200:                                          ; preds = %LeafBlock34.Flow200_crit_edge, %389
  br label %Flow202

Flow201:                                          ; preds = %LeafBlock32.Flow201_crit_edge, %411
  br label %428

428:                                              ; preds = %Flow202._crit_edge, %Flow201
  %429 = add i32 %.0.3.1, 1
  br label %Flow203

430:                                              ; preds = %Flow205
  %431 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %432 = insertelement <4 x float> poison, float %431, i64 0
  %433 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %434 = insertelement <4 x float> %432, float %433, i64 1
  %435 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %436 = insertelement <4 x float> %434, float %435, i64 2
  %437 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %438 = insertelement <4 x float> %436, float %437, i64 3
  %439 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %440 = fcmp uge float %431, %439
  br i1 %440, label %441, label %.Flow199_crit_edge

.Flow199_crit_edge:                               ; preds = %430
  br label %Flow199

441:                                              ; preds = %430
  %442 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %443 = call i32 @llvm.smin.i32(i32 %370, i32 %442)
  %444 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %445 = sext i32 %443 to i64
  %446 = getelementptr [4 x i8], ptr %2, i64 %445
  store float %444, ptr %446, align 4
  br label %448

Flow199:                                          ; preds = %.Flow199_crit_edge, %450
  %447 = phi i32 [ %451, %450 ], [ %370, %.Flow199_crit_edge ]
  br label %507

448:                                              ; preds = %Flow198._crit_edge, %441
  %.034.252 = phi i32 [ %454, %Flow198._crit_edge ], [ 0, %441 ]
  %449 = icmp slt i32 %.034.252, 2
  br i1 %449, label %452, label %.Flow198_crit_edge

.Flow198_crit_edge:                               ; preds = %448
  br label %Flow198

450:                                              ; preds = %Flow198
  %451 = add i32 %370, 1
  br label %Flow199

452:                                              ; preds = %448
  %453 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %456

Flow198:                                          ; preds = %.Flow198_crit_edge, %458
  %454 = phi i32 [ %459, %458 ], [ poison, %.Flow198_crit_edge ]
  %455 = phi i1 [ false, %458 ], [ true, %.Flow198_crit_edge ]
  br i1 %455, label %450, label %Flow198._crit_edge

Flow198._crit_edge:                               ; preds = %Flow198
  br label %448

456:                                              ; preds = %Flow197._crit_edge, %452
  %.0.253 = phi i32 [ %463, %Flow197._crit_edge ], [ %453, %452 ]
  %457 = icmp slt i32 %.0.253, 1
  br i1 %457, label %460, label %.Flow197_crit_edge

.Flow197_crit_edge:                               ; preds = %456
  br label %Flow197

458:                                              ; preds = %Flow197
  %459 = add i32 %.034.252, 1
  br label %Flow198

460:                                              ; preds = %456
  %461 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %462 = sub i32 %443, %461
  br label %NodeBlock42

Flow197:                                          ; preds = %.Flow197_crit_edge, %505
  %463 = phi i32 [ %506, %505 ], [ poison, %.Flow197_crit_edge ]
  %464 = phi i1 [ false, %505 ], [ true, %.Flow197_crit_edge ]
  br i1 %464, label %458, label %Flow197._crit_edge

Flow197._crit_edge:                               ; preds = %Flow197
  br label %456

NodeBlock42:                                      ; preds = %460
  %Pivot43 = icmp sge i32 %462, 2
  br i1 %Pivot43, label %LeafBlock40, label %NodeBlock42.Flow196_crit_edge

NodeBlock42.Flow196_crit_edge:                    ; preds = %NodeBlock42
  br label %Flow196

LeafBlock40:                                      ; preds = %NodeBlock42
  %SwitchLeaf41 = icmp eq i32 %462, 2
  br i1 %SwitchLeaf41, label %466, label %LeafBlock40.Flow194_crit_edge

LeafBlock40.Flow194_crit_edge:                    ; preds = %LeafBlock40
  br label %Flow194

Flow196:                                          ; preds = %NodeBlock42.Flow196_crit_edge, %Flow194
  %465 = phi i1 [ false, %Flow194 ], [ true, %NodeBlock42.Flow196_crit_edge ]
  br i1 %465, label %LeafBlock38, label %Flow196._crit_edge

Flow196._crit_edge:                               ; preds = %Flow196
  br label %505

LeafBlock38:                                      ; preds = %Flow196
  %SwitchLeaf39 = icmp eq i32 %462, 0
  br i1 %SwitchLeaf39, label %488, label %LeafBlock38.Flow195_crit_edge

LeafBlock38.Flow195_crit_edge:                    ; preds = %LeafBlock38
  br label %Flow195

466:                                              ; preds = %LeafBlock40
  %467 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %468 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %469 = sext i32 %467 to i64
  %470 = sext i32 %468 to i64
  %.split63 = getelementptr [16 x i8], ptr %1, i64 %469
  %471 = getelementptr [4 x i8], ptr %.split63, i64 %470
  %472 = load float, ptr %471, align 4
  %473 = sext i32 %443 to i64
  %474 = getelementptr [4 x i8], ptr %2, i64 %473
  %475 = load float, ptr %474, align 4
  %476 = call i1 @llvm.is.fpclass.f32(float %475, /* (sub) */ i32 144)
  %477 = call float @llvm.copysign.f32(float 0.000000e+00, float %475)
  %478 = select i1 %476, float %477, float %475
  %479 = call i1 @llvm.is.fpclass.f32(float %472, /* (sub) */ i32 144)
  %480 = call float @llvm.copysign.f32(float 0.000000e+00, float %472)
  %481 = select i1 %479, float %480, float %472
  %482 = fadd float %478, %481
  %483 = call i1 @llvm.is.fpclass.f32(float %482, /* (sub) */ i32 144)
  %484 = call float @llvm.copysign.f32(float 0.000000e+00, float %482)
  %485 = select i1 %483, float %484, float %482
  %486 = sext i32 %443 to i64
  %487 = getelementptr [4 x i8], ptr %2, i64 %486
  store float %485, ptr %487, align 4
  br label %Flow194

488:                                              ; preds = %LeafBlock38
  %489 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %490 = sext i32 %443 to i64
  %491 = getelementptr [4 x i8], ptr %2, i64 %490
  %492 = load float, ptr %491, align 4
  %493 = call i1 @llvm.is.fpclass.f32(float %492, /* (sub) */ i32 144)
  %494 = call float @llvm.copysign.f32(float 0.000000e+00, float %492)
  %495 = select i1 %493, float %494, float %492
  %496 = call i1 @llvm.is.fpclass.f32(float %489, /* (sub) */ i32 144)
  %497 = call float @llvm.copysign.f32(float 0.000000e+00, float %489)
  %498 = select i1 %496, float %497, float %489
  %499 = fadd float %495, %498
  %500 = call i1 @llvm.is.fpclass.f32(float %499, /* (sub) */ i32 144)
  %501 = call float @llvm.copysign.f32(float 0.000000e+00, float %499)
  %502 = select i1 %500, float %501, float %499
  %503 = sext i32 %443 to i64
  %504 = getelementptr [4 x i8], ptr %2, i64 %503
  store float %502, ptr %504, align 4
  br label %Flow195

Flow194:                                          ; preds = %LeafBlock40.Flow194_crit_edge, %466
  br label %Flow196

Flow195:                                          ; preds = %LeafBlock38.Flow195_crit_edge, %488
  br label %505

505:                                              ; preds = %Flow196._crit_edge, %Flow195
  %506 = add i32 %.0.253, 1
  br label %Flow197

507:                                              ; preds = %Flow199
  %508 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %509 = insertelement <4 x float> poison, float %508, i64 0
  %510 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %511 = insertelement <4 x float> %509, float %510, i64 1
  %512 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %513 = insertelement <4 x float> %511, float %512, i64 2
  %514 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %515 = insertelement <4 x float> %513, float %514, i64 3
  %516 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %517 = fcmp uge float %508, %516
  br i1 %517, label %518, label %.Flow193_crit_edge

.Flow193_crit_edge:                               ; preds = %507
  br label %Flow193

518:                                              ; preds = %507
  %519 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %520 = call i32 @llvm.smin.i32(i32 %447, i32 %519)
  %521 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %522 = sext i32 %520 to i64
  %523 = getelementptr [4 x i8], ptr %2, i64 %522
  store float %521, ptr %523, align 4
  br label %525

Flow193:                                          ; preds = %.Flow193_crit_edge, %527
  %524 = phi i32 [ %528, %527 ], [ %447, %.Flow193_crit_edge ]
  br label %584

525:                                              ; preds = %Flow192._crit_edge, %518
  %.034.1.2 = phi i32 [ %531, %Flow192._crit_edge ], [ 0, %518 ]
  %526 = icmp slt i32 %.034.1.2, 2
  br i1 %526, label %529, label %.Flow192_crit_edge

.Flow192_crit_edge:                               ; preds = %525
  br label %Flow192

527:                                              ; preds = %Flow192
  %528 = add i32 %447, 1
  br label %Flow193

529:                                              ; preds = %525
  %530 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %533

Flow192:                                          ; preds = %.Flow192_crit_edge, %535
  %531 = phi i32 [ %536, %535 ], [ poison, %.Flow192_crit_edge ]
  %532 = phi i1 [ false, %535 ], [ true, %.Flow192_crit_edge ]
  br i1 %532, label %527, label %Flow192._crit_edge

Flow192._crit_edge:                               ; preds = %Flow192
  br label %525

533:                                              ; preds = %Flow191._crit_edge, %529
  %.0.1.2 = phi i32 [ %540, %Flow191._crit_edge ], [ %530, %529 ]
  %534 = icmp slt i32 %.0.1.2, 2
  br i1 %534, label %537, label %.Flow191_crit_edge

.Flow191_crit_edge:                               ; preds = %533
  br label %Flow191

535:                                              ; preds = %Flow191
  %536 = add i32 %.034.1.2, 1
  br label %Flow192

537:                                              ; preds = %533
  %538 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %539 = sub i32 %520, %538
  br label %NodeBlock48

Flow191:                                          ; preds = %.Flow191_crit_edge, %582
  %540 = phi i32 [ %583, %582 ], [ poison, %.Flow191_crit_edge ]
  %541 = phi i1 [ false, %582 ], [ true, %.Flow191_crit_edge ]
  br i1 %541, label %535, label %Flow191._crit_edge

Flow191._crit_edge:                               ; preds = %Flow191
  br label %533

NodeBlock48:                                      ; preds = %537
  %Pivot49 = icmp sge i32 %539, 2
  br i1 %Pivot49, label %LeafBlock46, label %NodeBlock48.Flow190_crit_edge

NodeBlock48.Flow190_crit_edge:                    ; preds = %NodeBlock48
  br label %Flow190

LeafBlock46:                                      ; preds = %NodeBlock48
  %SwitchLeaf47 = icmp eq i32 %539, 2
  br i1 %SwitchLeaf47, label %543, label %LeafBlock46.Flow188_crit_edge

LeafBlock46.Flow188_crit_edge:                    ; preds = %LeafBlock46
  br label %Flow188

Flow190:                                          ; preds = %NodeBlock48.Flow190_crit_edge, %Flow188
  %542 = phi i1 [ false, %Flow188 ], [ true, %NodeBlock48.Flow190_crit_edge ]
  br i1 %542, label %LeafBlock44, label %Flow190._crit_edge

Flow190._crit_edge:                               ; preds = %Flow190
  br label %582

LeafBlock44:                                      ; preds = %Flow190
  %SwitchLeaf45 = icmp eq i32 %539, 0
  br i1 %SwitchLeaf45, label %565, label %LeafBlock44.Flow189_crit_edge

LeafBlock44.Flow189_crit_edge:                    ; preds = %LeafBlock44
  br label %Flow189

543:                                              ; preds = %LeafBlock46
  %544 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %545 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %546 = sext i32 %544 to i64
  %547 = sext i32 %545 to i64
  %.split64 = getelementptr [16 x i8], ptr %1, i64 %546
  %548 = getelementptr [4 x i8], ptr %.split64, i64 %547
  %549 = load float, ptr %548, align 4
  %550 = sext i32 %520 to i64
  %551 = getelementptr [4 x i8], ptr %2, i64 %550
  %552 = load float, ptr %551, align 4
  %553 = call i1 @llvm.is.fpclass.f32(float %552, /* (sub) */ i32 144)
  %554 = call float @llvm.copysign.f32(float 0.000000e+00, float %552)
  %555 = select i1 %553, float %554, float %552
  %556 = call i1 @llvm.is.fpclass.f32(float %549, /* (sub) */ i32 144)
  %557 = call float @llvm.copysign.f32(float 0.000000e+00, float %549)
  %558 = select i1 %556, float %557, float %549
  %559 = fadd float %555, %558
  %560 = call i1 @llvm.is.fpclass.f32(float %559, /* (sub) */ i32 144)
  %561 = call float @llvm.copysign.f32(float 0.000000e+00, float %559)
  %562 = select i1 %560, float %561, float %559
  %563 = sext i32 %520 to i64
  %564 = getelementptr [4 x i8], ptr %2, i64 %563
  store float %562, ptr %564, align 4
  br label %Flow188

565:                                              ; preds = %LeafBlock44
  %566 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %567 = sext i32 %520 to i64
  %568 = getelementptr [4 x i8], ptr %2, i64 %567
  %569 = load float, ptr %568, align 4
  %570 = call i1 @llvm.is.fpclass.f32(float %569, /* (sub) */ i32 144)
  %571 = call float @llvm.copysign.f32(float 0.000000e+00, float %569)
  %572 = select i1 %570, float %571, float %569
  %573 = call i1 @llvm.is.fpclass.f32(float %566, /* (sub) */ i32 144)
  %574 = call float @llvm.copysign.f32(float 0.000000e+00, float %566)
  %575 = select i1 %573, float %574, float %566
  %576 = fadd float %572, %575
  %577 = call i1 @llvm.is.fpclass.f32(float %576, /* (sub) */ i32 144)
  %578 = call float @llvm.copysign.f32(float 0.000000e+00, float %576)
  %579 = select i1 %577, float %578, float %576
  %580 = sext i32 %520 to i64
  %581 = getelementptr [4 x i8], ptr %2, i64 %580
  store float %579, ptr %581, align 4
  br label %Flow189

Flow188:                                          ; preds = %LeafBlock46.Flow188_crit_edge, %543
  br label %Flow190

Flow189:                                          ; preds = %LeafBlock44.Flow189_crit_edge, %565
  br label %582

582:                                              ; preds = %Flow190._crit_edge, %Flow189
  %583 = add i32 %.0.1.2, 1
  br label %Flow191

584:                                              ; preds = %Flow193
  %585 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %586 = insertelement <4 x float> poison, float %585, i64 0
  %587 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %588 = insertelement <4 x float> %586, float %587, i64 1
  %589 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %590 = insertelement <4 x float> %588, float %589, i64 2
  %591 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %592 = insertelement <4 x float> %590, float %591, i64 3
  %593 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %594 = fcmp uge float %585, %593
  br i1 %594, label %595, label %.Flow187_crit_edge

.Flow187_crit_edge:                               ; preds = %584
  br label %Flow187

595:                                              ; preds = %584
  %596 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %597 = call i32 @llvm.smin.i32(i32 %524, i32 %596)
  %598 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %599 = sext i32 %597 to i64
  %600 = getelementptr [4 x i8], ptr %2, i64 %599
  store float %598, ptr %600, align 4
  br label %602

Flow187:                                          ; preds = %.Flow187_crit_edge, %604
  %601 = phi i32 [ %605, %604 ], [ %524, %.Flow187_crit_edge ]
  br label %661

602:                                              ; preds = %Flow186._crit_edge, %595
  %.034.2.2 = phi i32 [ %608, %Flow186._crit_edge ], [ 0, %595 ]
  %603 = icmp slt i32 %.034.2.2, 2
  br i1 %603, label %606, label %.Flow186_crit_edge

.Flow186_crit_edge:                               ; preds = %602
  br label %Flow186

604:                                              ; preds = %Flow186
  %605 = add i32 %524, 1
  br label %Flow187

606:                                              ; preds = %602
  %607 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %610

Flow186:                                          ; preds = %.Flow186_crit_edge, %612
  %608 = phi i32 [ %613, %612 ], [ poison, %.Flow186_crit_edge ]
  %609 = phi i1 [ false, %612 ], [ true, %.Flow186_crit_edge ]
  br i1 %609, label %604, label %Flow186._crit_edge

Flow186._crit_edge:                               ; preds = %Flow186
  br label %602

610:                                              ; preds = %Flow185._crit_edge, %606
  %.0.2.2 = phi i32 [ %617, %Flow185._crit_edge ], [ %607, %606 ]
  %611 = icmp slt i32 %.0.2.2, 3
  br i1 %611, label %614, label %.Flow185_crit_edge

.Flow185_crit_edge:                               ; preds = %610
  br label %Flow185

612:                                              ; preds = %Flow185
  %613 = add i32 %.034.2.2, 1
  br label %Flow186

614:                                              ; preds = %610
  %615 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %616 = sub i32 %597, %615
  br label %NodeBlock54

Flow185:                                          ; preds = %.Flow185_crit_edge, %659
  %617 = phi i32 [ %660, %659 ], [ poison, %.Flow185_crit_edge ]
  %618 = phi i1 [ false, %659 ], [ true, %.Flow185_crit_edge ]
  br i1 %618, label %612, label %Flow185._crit_edge

Flow185._crit_edge:                               ; preds = %Flow185
  br label %610

NodeBlock54:                                      ; preds = %614
  %Pivot55 = icmp sge i32 %616, 2
  br i1 %Pivot55, label %LeafBlock52, label %NodeBlock54.Flow184_crit_edge

NodeBlock54.Flow184_crit_edge:                    ; preds = %NodeBlock54
  br label %Flow184

LeafBlock52:                                      ; preds = %NodeBlock54
  %SwitchLeaf53 = icmp eq i32 %616, 2
  br i1 %SwitchLeaf53, label %620, label %LeafBlock52.Flow182_crit_edge

LeafBlock52.Flow182_crit_edge:                    ; preds = %LeafBlock52
  br label %Flow182

Flow184:                                          ; preds = %NodeBlock54.Flow184_crit_edge, %Flow182
  %619 = phi i1 [ false, %Flow182 ], [ true, %NodeBlock54.Flow184_crit_edge ]
  br i1 %619, label %LeafBlock50, label %Flow184._crit_edge

Flow184._crit_edge:                               ; preds = %Flow184
  br label %659

LeafBlock50:                                      ; preds = %Flow184
  %SwitchLeaf51 = icmp eq i32 %616, 0
  br i1 %SwitchLeaf51, label %642, label %LeafBlock50.Flow183_crit_edge

LeafBlock50.Flow183_crit_edge:                    ; preds = %LeafBlock50
  br label %Flow183

620:                                              ; preds = %LeafBlock52
  %621 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %622 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %623 = sext i32 %621 to i64
  %624 = sext i32 %622 to i64
  %.split65 = getelementptr [16 x i8], ptr %1, i64 %623
  %625 = getelementptr [4 x i8], ptr %.split65, i64 %624
  %626 = load float, ptr %625, align 4
  %627 = sext i32 %597 to i64
  %628 = getelementptr [4 x i8], ptr %2, i64 %627
  %629 = load float, ptr %628, align 4
  %630 = call i1 @llvm.is.fpclass.f32(float %629, /* (sub) */ i32 144)
  %631 = call float @llvm.copysign.f32(float 0.000000e+00, float %629)
  %632 = select i1 %630, float %631, float %629
  %633 = call i1 @llvm.is.fpclass.f32(float %626, /* (sub) */ i32 144)
  %634 = call float @llvm.copysign.f32(float 0.000000e+00, float %626)
  %635 = select i1 %633, float %634, float %626
  %636 = fadd float %632, %635
  %637 = call i1 @llvm.is.fpclass.f32(float %636, /* (sub) */ i32 144)
  %638 = call float @llvm.copysign.f32(float 0.000000e+00, float %636)
  %639 = select i1 %637, float %638, float %636
  %640 = sext i32 %597 to i64
  %641 = getelementptr [4 x i8], ptr %2, i64 %640
  store float %639, ptr %641, align 4
  br label %Flow182

642:                                              ; preds = %LeafBlock50
  %643 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %644 = sext i32 %597 to i64
  %645 = getelementptr [4 x i8], ptr %2, i64 %644
  %646 = load float, ptr %645, align 4
  %647 = call i1 @llvm.is.fpclass.f32(float %646, /* (sub) */ i32 144)
  %648 = call float @llvm.copysign.f32(float 0.000000e+00, float %646)
  %649 = select i1 %647, float %648, float %646
  %650 = call i1 @llvm.is.fpclass.f32(float %643, /* (sub) */ i32 144)
  %651 = call float @llvm.copysign.f32(float 0.000000e+00, float %643)
  %652 = select i1 %650, float %651, float %643
  %653 = fadd float %649, %652
  %654 = call i1 @llvm.is.fpclass.f32(float %653, /* (sub) */ i32 144)
  %655 = call float @llvm.copysign.f32(float 0.000000e+00, float %653)
  %656 = select i1 %654, float %655, float %653
  %657 = sext i32 %597 to i64
  %658 = getelementptr [4 x i8], ptr %2, i64 %657
  store float %656, ptr %658, align 4
  br label %Flow183

Flow182:                                          ; preds = %LeafBlock52.Flow182_crit_edge, %620
  br label %Flow184

Flow183:                                          ; preds = %LeafBlock50.Flow183_crit_edge, %642
  br label %659

659:                                              ; preds = %Flow184._crit_edge, %Flow183
  %660 = add i32 %.0.2.2, 1
  br label %Flow185

661:                                              ; preds = %Flow187
  %662 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %663 = insertelement <4 x float> poison, float %662, i64 0
  %664 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %665 = insertelement <4 x float> %663, float %664, i64 1
  %666 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %667 = insertelement <4 x float> %665, float %666, i64 2
  %668 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %669 = insertelement <4 x float> %667, float %668, i64 3
  %670 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %671 = fcmp uge float %662, %670
  br i1 %671, label %672, label %.Flow181_crit_edge

.Flow181_crit_edge:                               ; preds = %661
  br label %Flow181

672:                                              ; preds = %661
  %673 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %674 = call i32 @llvm.smin.i32(i32 %601, i32 %673)
  %675 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %676 = sext i32 %674 to i64
  %677 = getelementptr [4 x i8], ptr %2, i64 %676
  store float %675, ptr %677, align 4
  br label %679

Flow181:                                          ; preds = %.Flow181_crit_edge, %681
  %678 = phi i32 [ %682, %681 ], [ %601, %.Flow181_crit_edge ]
  br label %738

679:                                              ; preds = %Flow180._crit_edge, %672
  %.034.3.2 = phi i32 [ %685, %Flow180._crit_edge ], [ 0, %672 ]
  %680 = icmp slt i32 %.034.3.2, 2
  br i1 %680, label %683, label %.Flow180_crit_edge

.Flow180_crit_edge:                               ; preds = %679
  br label %Flow180

681:                                              ; preds = %Flow180
  %682 = add i32 %601, 1
  br label %Flow181

683:                                              ; preds = %679
  %684 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %687

Flow180:                                          ; preds = %.Flow180_crit_edge, %689
  %685 = phi i32 [ %690, %689 ], [ poison, %.Flow180_crit_edge ]
  %686 = phi i1 [ false, %689 ], [ true, %.Flow180_crit_edge ]
  br i1 %686, label %681, label %Flow180._crit_edge

Flow180._crit_edge:                               ; preds = %Flow180
  br label %679

687:                                              ; preds = %Flow179._crit_edge, %683
  %.0.3.2 = phi i32 [ %694, %Flow179._crit_edge ], [ %684, %683 ]
  %688 = icmp slt i32 %.0.3.2, 4
  br i1 %688, label %691, label %.Flow179_crit_edge

.Flow179_crit_edge:                               ; preds = %687
  br label %Flow179

689:                                              ; preds = %Flow179
  %690 = add i32 %.034.3.2, 1
  br label %Flow180

691:                                              ; preds = %687
  %692 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %693 = sub i32 %674, %692
  br label %NodeBlock60

Flow179:                                          ; preds = %.Flow179_crit_edge, %736
  %694 = phi i32 [ %737, %736 ], [ poison, %.Flow179_crit_edge ]
  %695 = phi i1 [ false, %736 ], [ true, %.Flow179_crit_edge ]
  br i1 %695, label %689, label %Flow179._crit_edge

Flow179._crit_edge:                               ; preds = %Flow179
  br label %687

NodeBlock60:                                      ; preds = %691
  %Pivot61 = icmp sge i32 %693, 2
  br i1 %Pivot61, label %LeafBlock58, label %NodeBlock60.Flow178_crit_edge

NodeBlock60.Flow178_crit_edge:                    ; preds = %NodeBlock60
  br label %Flow178

LeafBlock58:                                      ; preds = %NodeBlock60
  %SwitchLeaf59 = icmp eq i32 %693, 2
  br i1 %SwitchLeaf59, label %697, label %LeafBlock58.Flow176_crit_edge

LeafBlock58.Flow176_crit_edge:                    ; preds = %LeafBlock58
  br label %Flow176

Flow178:                                          ; preds = %NodeBlock60.Flow178_crit_edge, %Flow176
  %696 = phi i1 [ false, %Flow176 ], [ true, %NodeBlock60.Flow178_crit_edge ]
  br i1 %696, label %LeafBlock56, label %Flow178._crit_edge

Flow178._crit_edge:                               ; preds = %Flow178
  br label %736

LeafBlock56:                                      ; preds = %Flow178
  %SwitchLeaf57 = icmp eq i32 %693, 0
  br i1 %SwitchLeaf57, label %719, label %LeafBlock56.Flow177_crit_edge

LeafBlock56.Flow177_crit_edge:                    ; preds = %LeafBlock56
  br label %Flow177

697:                                              ; preds = %LeafBlock58
  %698 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %699 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %700 = sext i32 %698 to i64
  %701 = sext i32 %699 to i64
  %.split66 = getelementptr [16 x i8], ptr %1, i64 %700
  %702 = getelementptr [4 x i8], ptr %.split66, i64 %701
  %703 = load float, ptr %702, align 4
  %704 = sext i32 %674 to i64
  %705 = getelementptr [4 x i8], ptr %2, i64 %704
  %706 = load float, ptr %705, align 4
  %707 = call i1 @llvm.is.fpclass.f32(float %706, /* (sub) */ i32 144)
  %708 = call float @llvm.copysign.f32(float 0.000000e+00, float %706)
  %709 = select i1 %707, float %708, float %706
  %710 = call i1 @llvm.is.fpclass.f32(float %703, /* (sub) */ i32 144)
  %711 = call float @llvm.copysign.f32(float 0.000000e+00, float %703)
  %712 = select i1 %710, float %711, float %703
  %713 = fadd float %709, %712
  %714 = call i1 @llvm.is.fpclass.f32(float %713, /* (sub) */ i32 144)
  %715 = call float @llvm.copysign.f32(float 0.000000e+00, float %713)
  %716 = select i1 %714, float %715, float %713
  %717 = sext i32 %674 to i64
  %718 = getelementptr [4 x i8], ptr %2, i64 %717
  store float %716, ptr %718, align 4
  br label %Flow176

719:                                              ; preds = %LeafBlock56
  %720 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %721 = sext i32 %674 to i64
  %722 = getelementptr [4 x i8], ptr %2, i64 %721
  %723 = load float, ptr %722, align 4
  %724 = call i1 @llvm.is.fpclass.f32(float %723, /* (sub) */ i32 144)
  %725 = call float @llvm.copysign.f32(float 0.000000e+00, float %723)
  %726 = select i1 %724, float %725, float %723
  %727 = call i1 @llvm.is.fpclass.f32(float %720, /* (sub) */ i32 144)
  %728 = call float @llvm.copysign.f32(float 0.000000e+00, float %720)
  %729 = select i1 %727, float %728, float %720
  %730 = fadd float %726, %729
  %731 = call i1 @llvm.is.fpclass.f32(float %730, /* (sub) */ i32 144)
  %732 = call float @llvm.copysign.f32(float 0.000000e+00, float %730)
  %733 = select i1 %731, float %732, float %730
  %734 = sext i32 %674 to i64
  %735 = getelementptr [4 x i8], ptr %2, i64 %734
  store float %733, ptr %735, align 4
  br label %Flow177

Flow176:                                          ; preds = %LeafBlock58.Flow176_crit_edge, %697
  br label %Flow178

Flow177:                                          ; preds = %LeafBlock56.Flow177_crit_edge, %719
  br label %736

736:                                              ; preds = %Flow178._crit_edge, %Flow177
  %737 = add i32 %.0.3.2, 1
  br label %Flow179

738:                                              ; preds = %Flow181
  %739 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %740 = insertelement <4 x float> poison, float %739, i64 0
  %741 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %742 = insertelement <4 x float> %740, float %741, i64 1
  %743 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %744 = insertelement <4 x float> %742, float %743, i64 2
  %745 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %746 = insertelement <4 x float> %744, float %745, i64 3
  %747 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %748 = fcmp uge float %739, %747
  br i1 %748, label %749, label %.Flow175_crit_edge

.Flow175_crit_edge:                               ; preds = %738
  br label %Flow175

749:                                              ; preds = %738
  %750 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %751 = call i32 @llvm.smin.i32(i32 %678, i32 %750)
  %752 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %753 = sext i32 %751 to i64
  %754 = getelementptr [4 x i8], ptr %2, i64 %753
  store float %752, ptr %754, align 4
  br label %756

Flow175:                                          ; preds = %.Flow175_crit_edge, %758
  %755 = phi i32 [ %759, %758 ], [ %678, %.Flow175_crit_edge ]
  br label %815

756:                                              ; preds = %Flow174._crit_edge, %749
  %.034.355 = phi i32 [ %762, %Flow174._crit_edge ], [ 0, %749 ]
  %757 = icmp slt i32 %.034.355, 3
  br i1 %757, label %760, label %.Flow174_crit_edge

.Flow174_crit_edge:                               ; preds = %756
  br label %Flow174

758:                                              ; preds = %Flow174
  %759 = add i32 %678, 1
  br label %Flow175

760:                                              ; preds = %756
  %761 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %764

Flow174:                                          ; preds = %.Flow174_crit_edge, %766
  %762 = phi i32 [ %767, %766 ], [ poison, %.Flow174_crit_edge ]
  %763 = phi i1 [ false, %766 ], [ true, %.Flow174_crit_edge ]
  br i1 %763, label %758, label %Flow174._crit_edge

Flow174._crit_edge:                               ; preds = %Flow174
  br label %756

764:                                              ; preds = %Flow173._crit_edge, %760
  %.0.356 = phi i32 [ %771, %Flow173._crit_edge ], [ %761, %760 ]
  %765 = icmp slt i32 %.0.356, 1
  br i1 %765, label %768, label %.Flow173_crit_edge

.Flow173_crit_edge:                               ; preds = %764
  br label %Flow173

766:                                              ; preds = %Flow173
  %767 = add i32 %.034.355, 1
  br label %Flow174

768:                                              ; preds = %764
  %769 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %770 = sub i32 %751, %769
  br label %NodeBlock66

Flow173:                                          ; preds = %.Flow173_crit_edge, %813
  %771 = phi i32 [ %814, %813 ], [ poison, %.Flow173_crit_edge ]
  %772 = phi i1 [ false, %813 ], [ true, %.Flow173_crit_edge ]
  br i1 %772, label %766, label %Flow173._crit_edge

Flow173._crit_edge:                               ; preds = %Flow173
  br label %764

NodeBlock66:                                      ; preds = %768
  %Pivot67 = icmp sge i32 %770, 2
  br i1 %Pivot67, label %LeafBlock64, label %NodeBlock66.Flow172_crit_edge

NodeBlock66.Flow172_crit_edge:                    ; preds = %NodeBlock66
  br label %Flow172

LeafBlock64:                                      ; preds = %NodeBlock66
  %SwitchLeaf65 = icmp eq i32 %770, 2
  br i1 %SwitchLeaf65, label %774, label %LeafBlock64.Flow170_crit_edge

LeafBlock64.Flow170_crit_edge:                    ; preds = %LeafBlock64
  br label %Flow170

Flow172:                                          ; preds = %NodeBlock66.Flow172_crit_edge, %Flow170
  %773 = phi i1 [ false, %Flow170 ], [ true, %NodeBlock66.Flow172_crit_edge ]
  br i1 %773, label %LeafBlock62, label %Flow172._crit_edge

Flow172._crit_edge:                               ; preds = %Flow172
  br label %813

LeafBlock62:                                      ; preds = %Flow172
  %SwitchLeaf63 = icmp eq i32 %770, 0
  br i1 %SwitchLeaf63, label %796, label %LeafBlock62.Flow171_crit_edge

LeafBlock62.Flow171_crit_edge:                    ; preds = %LeafBlock62
  br label %Flow171

774:                                              ; preds = %LeafBlock64
  %775 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %776 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %777 = sext i32 %775 to i64
  %778 = sext i32 %776 to i64
  %.split67 = getelementptr [16 x i8], ptr %1, i64 %777
  %779 = getelementptr [4 x i8], ptr %.split67, i64 %778
  %780 = load float, ptr %779, align 4
  %781 = sext i32 %751 to i64
  %782 = getelementptr [4 x i8], ptr %2, i64 %781
  %783 = load float, ptr %782, align 4
  %784 = call i1 @llvm.is.fpclass.f32(float %783, /* (sub) */ i32 144)
  %785 = call float @llvm.copysign.f32(float 0.000000e+00, float %783)
  %786 = select i1 %784, float %785, float %783
  %787 = call i1 @llvm.is.fpclass.f32(float %780, /* (sub) */ i32 144)
  %788 = call float @llvm.copysign.f32(float 0.000000e+00, float %780)
  %789 = select i1 %787, float %788, float %780
  %790 = fadd float %786, %789
  %791 = call i1 @llvm.is.fpclass.f32(float %790, /* (sub) */ i32 144)
  %792 = call float @llvm.copysign.f32(float 0.000000e+00, float %790)
  %793 = select i1 %791, float %792, float %790
  %794 = sext i32 %751 to i64
  %795 = getelementptr [4 x i8], ptr %2, i64 %794
  store float %793, ptr %795, align 4
  br label %Flow170

796:                                              ; preds = %LeafBlock62
  %797 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %798 = sext i32 %751 to i64
  %799 = getelementptr [4 x i8], ptr %2, i64 %798
  %800 = load float, ptr %799, align 4
  %801 = call i1 @llvm.is.fpclass.f32(float %800, /* (sub) */ i32 144)
  %802 = call float @llvm.copysign.f32(float 0.000000e+00, float %800)
  %803 = select i1 %801, float %802, float %800
  %804 = call i1 @llvm.is.fpclass.f32(float %797, /* (sub) */ i32 144)
  %805 = call float @llvm.copysign.f32(float 0.000000e+00, float %797)
  %806 = select i1 %804, float %805, float %797
  %807 = fadd float %803, %806
  %808 = call i1 @llvm.is.fpclass.f32(float %807, /* (sub) */ i32 144)
  %809 = call float @llvm.copysign.f32(float 0.000000e+00, float %807)
  %810 = select i1 %808, float %809, float %807
  %811 = sext i32 %751 to i64
  %812 = getelementptr [4 x i8], ptr %2, i64 %811
  store float %810, ptr %812, align 4
  br label %Flow171

Flow170:                                          ; preds = %LeafBlock64.Flow170_crit_edge, %774
  br label %Flow172

Flow171:                                          ; preds = %LeafBlock62.Flow171_crit_edge, %796
  br label %813

813:                                              ; preds = %Flow172._crit_edge, %Flow171
  %814 = add i32 %.0.356, 1
  br label %Flow173

815:                                              ; preds = %Flow175
  %816 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %817 = insertelement <4 x float> poison, float %816, i64 0
  %818 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %819 = insertelement <4 x float> %817, float %818, i64 1
  %820 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %821 = insertelement <4 x float> %819, float %820, i64 2
  %822 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %823 = insertelement <4 x float> %821, float %822, i64 3
  %824 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %825 = fcmp uge float %816, %824
  br i1 %825, label %826, label %.Flow169_crit_edge

.Flow169_crit_edge:                               ; preds = %815
  br label %Flow169

826:                                              ; preds = %815
  %827 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %828 = call i32 @llvm.smin.i32(i32 %755, i32 %827)
  %829 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %830 = sext i32 %828 to i64
  %831 = getelementptr [4 x i8], ptr %2, i64 %830
  store float %829, ptr %831, align 4
  br label %833

Flow169:                                          ; preds = %.Flow169_crit_edge, %835
  %832 = phi i32 [ %836, %835 ], [ %755, %.Flow169_crit_edge ]
  br label %892

833:                                              ; preds = %Flow168._crit_edge, %826
  %.034.1.3 = phi i32 [ %839, %Flow168._crit_edge ], [ 0, %826 ]
  %834 = icmp slt i32 %.034.1.3, 3
  br i1 %834, label %837, label %.Flow168_crit_edge

.Flow168_crit_edge:                               ; preds = %833
  br label %Flow168

835:                                              ; preds = %Flow168
  %836 = add i32 %755, 1
  br label %Flow169

837:                                              ; preds = %833
  %838 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %841

Flow168:                                          ; preds = %.Flow168_crit_edge, %843
  %839 = phi i32 [ %844, %843 ], [ poison, %.Flow168_crit_edge ]
  %840 = phi i1 [ false, %843 ], [ true, %.Flow168_crit_edge ]
  br i1 %840, label %835, label %Flow168._crit_edge

Flow168._crit_edge:                               ; preds = %Flow168
  br label %833

841:                                              ; preds = %Flow167._crit_edge, %837
  %.0.1.3 = phi i32 [ %848, %Flow167._crit_edge ], [ %838, %837 ]
  %842 = icmp slt i32 %.0.1.3, 2
  br i1 %842, label %845, label %.Flow167_crit_edge

.Flow167_crit_edge:                               ; preds = %841
  br label %Flow167

843:                                              ; preds = %Flow167
  %844 = add i32 %.034.1.3, 1
  br label %Flow168

845:                                              ; preds = %841
  %846 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %847 = sub i32 %828, %846
  br label %NodeBlock72

Flow167:                                          ; preds = %.Flow167_crit_edge, %890
  %848 = phi i32 [ %891, %890 ], [ poison, %.Flow167_crit_edge ]
  %849 = phi i1 [ false, %890 ], [ true, %.Flow167_crit_edge ]
  br i1 %849, label %843, label %Flow167._crit_edge

Flow167._crit_edge:                               ; preds = %Flow167
  br label %841

NodeBlock72:                                      ; preds = %845
  %Pivot73 = icmp sge i32 %847, 2
  br i1 %Pivot73, label %LeafBlock70, label %NodeBlock72.Flow166_crit_edge

NodeBlock72.Flow166_crit_edge:                    ; preds = %NodeBlock72
  br label %Flow166

LeafBlock70:                                      ; preds = %NodeBlock72
  %SwitchLeaf71 = icmp eq i32 %847, 2
  br i1 %SwitchLeaf71, label %851, label %LeafBlock70.Flow164_crit_edge

LeafBlock70.Flow164_crit_edge:                    ; preds = %LeafBlock70
  br label %Flow164

Flow166:                                          ; preds = %NodeBlock72.Flow166_crit_edge, %Flow164
  %850 = phi i1 [ false, %Flow164 ], [ true, %NodeBlock72.Flow166_crit_edge ]
  br i1 %850, label %LeafBlock68, label %Flow166._crit_edge

Flow166._crit_edge:                               ; preds = %Flow166
  br label %890

LeafBlock68:                                      ; preds = %Flow166
  %SwitchLeaf69 = icmp eq i32 %847, 0
  br i1 %SwitchLeaf69, label %873, label %LeafBlock68.Flow165_crit_edge

LeafBlock68.Flow165_crit_edge:                    ; preds = %LeafBlock68
  br label %Flow165

851:                                              ; preds = %LeafBlock70
  %852 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %853 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %854 = sext i32 %852 to i64
  %855 = sext i32 %853 to i64
  %.split68 = getelementptr [16 x i8], ptr %1, i64 %854
  %856 = getelementptr [4 x i8], ptr %.split68, i64 %855
  %857 = load float, ptr %856, align 4
  %858 = sext i32 %828 to i64
  %859 = getelementptr [4 x i8], ptr %2, i64 %858
  %860 = load float, ptr %859, align 4
  %861 = call i1 @llvm.is.fpclass.f32(float %860, /* (sub) */ i32 144)
  %862 = call float @llvm.copysign.f32(float 0.000000e+00, float %860)
  %863 = select i1 %861, float %862, float %860
  %864 = call i1 @llvm.is.fpclass.f32(float %857, /* (sub) */ i32 144)
  %865 = call float @llvm.copysign.f32(float 0.000000e+00, float %857)
  %866 = select i1 %864, float %865, float %857
  %867 = fadd float %863, %866
  %868 = call i1 @llvm.is.fpclass.f32(float %867, /* (sub) */ i32 144)
  %869 = call float @llvm.copysign.f32(float 0.000000e+00, float %867)
  %870 = select i1 %868, float %869, float %867
  %871 = sext i32 %828 to i64
  %872 = getelementptr [4 x i8], ptr %2, i64 %871
  store float %870, ptr %872, align 4
  br label %Flow164

873:                                              ; preds = %LeafBlock68
  %874 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %875 = sext i32 %828 to i64
  %876 = getelementptr [4 x i8], ptr %2, i64 %875
  %877 = load float, ptr %876, align 4
  %878 = call i1 @llvm.is.fpclass.f32(float %877, /* (sub) */ i32 144)
  %879 = call float @llvm.copysign.f32(float 0.000000e+00, float %877)
  %880 = select i1 %878, float %879, float %877
  %881 = call i1 @llvm.is.fpclass.f32(float %874, /* (sub) */ i32 144)
  %882 = call float @llvm.copysign.f32(float 0.000000e+00, float %874)
  %883 = select i1 %881, float %882, float %874
  %884 = fadd float %880, %883
  %885 = call i1 @llvm.is.fpclass.f32(float %884, /* (sub) */ i32 144)
  %886 = call float @llvm.copysign.f32(float 0.000000e+00, float %884)
  %887 = select i1 %885, float %886, float %884
  %888 = sext i32 %828 to i64
  %889 = getelementptr [4 x i8], ptr %2, i64 %888
  store float %887, ptr %889, align 4
  br label %Flow165

Flow164:                                          ; preds = %LeafBlock70.Flow164_crit_edge, %851
  br label %Flow166

Flow165:                                          ; preds = %LeafBlock68.Flow165_crit_edge, %873
  br label %890

890:                                              ; preds = %Flow166._crit_edge, %Flow165
  %891 = add i32 %.0.1.3, 1
  br label %Flow167

892:                                              ; preds = %Flow169
  %893 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %894 = insertelement <4 x float> poison, float %893, i64 0
  %895 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %896 = insertelement <4 x float> %894, float %895, i64 1
  %897 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %898 = insertelement <4 x float> %896, float %897, i64 2
  %899 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %900 = insertelement <4 x float> %898, float %899, i64 3
  %901 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %902 = fcmp uge float %893, %901
  br i1 %902, label %903, label %.Flow163_crit_edge

.Flow163_crit_edge:                               ; preds = %892
  br label %Flow163

903:                                              ; preds = %892
  %904 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %905 = call i32 @llvm.smin.i32(i32 %832, i32 %904)
  %906 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %907 = sext i32 %905 to i64
  %908 = getelementptr [4 x i8], ptr %2, i64 %907
  store float %906, ptr %908, align 4
  br label %910

Flow163:                                          ; preds = %.Flow163_crit_edge, %912
  %909 = phi i32 [ %913, %912 ], [ %832, %.Flow163_crit_edge ]
  br label %969

910:                                              ; preds = %Flow162._crit_edge, %903
  %.034.2.3 = phi i32 [ %916, %Flow162._crit_edge ], [ 0, %903 ]
  %911 = icmp slt i32 %.034.2.3, 3
  br i1 %911, label %914, label %.Flow162_crit_edge

.Flow162_crit_edge:                               ; preds = %910
  br label %Flow162

912:                                              ; preds = %Flow162
  %913 = add i32 %832, 1
  br label %Flow163

914:                                              ; preds = %910
  %915 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %918

Flow162:                                          ; preds = %.Flow162_crit_edge, %920
  %916 = phi i32 [ %921, %920 ], [ poison, %.Flow162_crit_edge ]
  %917 = phi i1 [ false, %920 ], [ true, %.Flow162_crit_edge ]
  br i1 %917, label %912, label %Flow162._crit_edge

Flow162._crit_edge:                               ; preds = %Flow162
  br label %910

918:                                              ; preds = %Flow161._crit_edge, %914
  %.0.2.3 = phi i32 [ %925, %Flow161._crit_edge ], [ %915, %914 ]
  %919 = icmp slt i32 %.0.2.3, 3
  br i1 %919, label %922, label %.Flow161_crit_edge

.Flow161_crit_edge:                               ; preds = %918
  br label %Flow161

920:                                              ; preds = %Flow161
  %921 = add i32 %.034.2.3, 1
  br label %Flow162

922:                                              ; preds = %918
  %923 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %924 = sub i32 %905, %923
  br label %NodeBlock78

Flow161:                                          ; preds = %.Flow161_crit_edge, %967
  %925 = phi i32 [ %968, %967 ], [ poison, %.Flow161_crit_edge ]
  %926 = phi i1 [ false, %967 ], [ true, %.Flow161_crit_edge ]
  br i1 %926, label %920, label %Flow161._crit_edge

Flow161._crit_edge:                               ; preds = %Flow161
  br label %918

NodeBlock78:                                      ; preds = %922
  %Pivot79 = icmp sge i32 %924, 2
  br i1 %Pivot79, label %LeafBlock76, label %NodeBlock78.Flow160_crit_edge

NodeBlock78.Flow160_crit_edge:                    ; preds = %NodeBlock78
  br label %Flow160

LeafBlock76:                                      ; preds = %NodeBlock78
  %SwitchLeaf77 = icmp eq i32 %924, 2
  br i1 %SwitchLeaf77, label %928, label %LeafBlock76.Flow158_crit_edge

LeafBlock76.Flow158_crit_edge:                    ; preds = %LeafBlock76
  br label %Flow158

Flow160:                                          ; preds = %NodeBlock78.Flow160_crit_edge, %Flow158
  %927 = phi i1 [ false, %Flow158 ], [ true, %NodeBlock78.Flow160_crit_edge ]
  br i1 %927, label %LeafBlock74, label %Flow160._crit_edge

Flow160._crit_edge:                               ; preds = %Flow160
  br label %967

LeafBlock74:                                      ; preds = %Flow160
  %SwitchLeaf75 = icmp eq i32 %924, 0
  br i1 %SwitchLeaf75, label %950, label %LeafBlock74.Flow159_crit_edge

LeafBlock74.Flow159_crit_edge:                    ; preds = %LeafBlock74
  br label %Flow159

928:                                              ; preds = %LeafBlock76
  %929 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %930 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %931 = sext i32 %929 to i64
  %932 = sext i32 %930 to i64
  %.split69 = getelementptr [16 x i8], ptr %1, i64 %931
  %933 = getelementptr [4 x i8], ptr %.split69, i64 %932
  %934 = load float, ptr %933, align 4
  %935 = sext i32 %905 to i64
  %936 = getelementptr [4 x i8], ptr %2, i64 %935
  %937 = load float, ptr %936, align 4
  %938 = call i1 @llvm.is.fpclass.f32(float %937, /* (sub) */ i32 144)
  %939 = call float @llvm.copysign.f32(float 0.000000e+00, float %937)
  %940 = select i1 %938, float %939, float %937
  %941 = call i1 @llvm.is.fpclass.f32(float %934, /* (sub) */ i32 144)
  %942 = call float @llvm.copysign.f32(float 0.000000e+00, float %934)
  %943 = select i1 %941, float %942, float %934
  %944 = fadd float %940, %943
  %945 = call i1 @llvm.is.fpclass.f32(float %944, /* (sub) */ i32 144)
  %946 = call float @llvm.copysign.f32(float 0.000000e+00, float %944)
  %947 = select i1 %945, float %946, float %944
  %948 = sext i32 %905 to i64
  %949 = getelementptr [4 x i8], ptr %2, i64 %948
  store float %947, ptr %949, align 4
  br label %Flow158

950:                                              ; preds = %LeafBlock74
  %951 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %952 = sext i32 %905 to i64
  %953 = getelementptr [4 x i8], ptr %2, i64 %952
  %954 = load float, ptr %953, align 4
  %955 = call i1 @llvm.is.fpclass.f32(float %954, /* (sub) */ i32 144)
  %956 = call float @llvm.copysign.f32(float 0.000000e+00, float %954)
  %957 = select i1 %955, float %956, float %954
  %958 = call i1 @llvm.is.fpclass.f32(float %951, /* (sub) */ i32 144)
  %959 = call float @llvm.copysign.f32(float 0.000000e+00, float %951)
  %960 = select i1 %958, float %959, float %951
  %961 = fadd float %957, %960
  %962 = call i1 @llvm.is.fpclass.f32(float %961, /* (sub) */ i32 144)
  %963 = call float @llvm.copysign.f32(float 0.000000e+00, float %961)
  %964 = select i1 %962, float %963, float %961
  %965 = sext i32 %905 to i64
  %966 = getelementptr [4 x i8], ptr %2, i64 %965
  store float %964, ptr %966, align 4
  br label %Flow159

Flow158:                                          ; preds = %LeafBlock76.Flow158_crit_edge, %928
  br label %Flow160

Flow159:                                          ; preds = %LeafBlock74.Flow159_crit_edge, %950
  br label %967

967:                                              ; preds = %Flow160._crit_edge, %Flow159
  %968 = add i32 %.0.2.3, 1
  br label %Flow161

969:                                              ; preds = %Flow163
  %970 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %971 = insertelement <4 x float> poison, float %970, i64 0
  %972 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %973 = insertelement <4 x float> %971, float %972, i64 1
  %974 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %975 = insertelement <4 x float> %973, float %974, i64 2
  %976 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %977 = insertelement <4 x float> %975, float %976, i64 3
  %978 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %979 = fcmp uge float %970, %978
  br i1 %979, label %980, label %.Flow157_crit_edge

.Flow157_crit_edge:                               ; preds = %969
  br label %Flow157

980:                                              ; preds = %969
  %981 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %982 = call i32 @llvm.smin.i32(i32 %909, i32 %981)
  %983 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %984 = sext i32 %982 to i64
  %985 = getelementptr [4 x i8], ptr %2, i64 %984
  store float %983, ptr %985, align 4
  br label %987

Flow157:                                          ; preds = %.Flow157_crit_edge, %989
  %986 = phi i32 [ %990, %989 ], [ %909, %.Flow157_crit_edge ]
  br label %1046

987:                                              ; preds = %Flow156._crit_edge, %980
  %.034.3.3 = phi i32 [ %993, %Flow156._crit_edge ], [ 0, %980 ]
  %988 = icmp slt i32 %.034.3.3, 3
  br i1 %988, label %991, label %.Flow156_crit_edge

.Flow156_crit_edge:                               ; preds = %987
  br label %Flow156

989:                                              ; preds = %Flow156
  %990 = add i32 %909, 1
  br label %Flow157

991:                                              ; preds = %987
  %992 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %995

Flow156:                                          ; preds = %.Flow156_crit_edge, %997
  %993 = phi i32 [ %998, %997 ], [ poison, %.Flow156_crit_edge ]
  %994 = phi i1 [ false, %997 ], [ true, %.Flow156_crit_edge ]
  br i1 %994, label %989, label %Flow156._crit_edge

Flow156._crit_edge:                               ; preds = %Flow156
  br label %987

995:                                              ; preds = %Flow155._crit_edge, %991
  %.0.3.3 = phi i32 [ %1002, %Flow155._crit_edge ], [ %992, %991 ]
  %996 = icmp slt i32 %.0.3.3, 4
  br i1 %996, label %999, label %.Flow155_crit_edge

.Flow155_crit_edge:                               ; preds = %995
  br label %Flow155

997:                                              ; preds = %Flow155
  %998 = add i32 %.034.3.3, 1
  br label %Flow156

999:                                              ; preds = %995
  %1000 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %1001 = sub i32 %982, %1000
  br label %NodeBlock84

Flow155:                                          ; preds = %.Flow155_crit_edge, %1044
  %1002 = phi i32 [ %1045, %1044 ], [ poison, %.Flow155_crit_edge ]
  %1003 = phi i1 [ false, %1044 ], [ true, %.Flow155_crit_edge ]
  br i1 %1003, label %997, label %Flow155._crit_edge

Flow155._crit_edge:                               ; preds = %Flow155
  br label %995

NodeBlock84:                                      ; preds = %999
  %Pivot85 = icmp sge i32 %1001, 2
  br i1 %Pivot85, label %LeafBlock82, label %NodeBlock84.Flow154_crit_edge

NodeBlock84.Flow154_crit_edge:                    ; preds = %NodeBlock84
  br label %Flow154

LeafBlock82:                                      ; preds = %NodeBlock84
  %SwitchLeaf83 = icmp eq i32 %1001, 2
  br i1 %SwitchLeaf83, label %1005, label %LeafBlock82.Flow152_crit_edge

LeafBlock82.Flow152_crit_edge:                    ; preds = %LeafBlock82
  br label %Flow152

Flow154:                                          ; preds = %NodeBlock84.Flow154_crit_edge, %Flow152
  %1004 = phi i1 [ false, %Flow152 ], [ true, %NodeBlock84.Flow154_crit_edge ]
  br i1 %1004, label %LeafBlock80, label %Flow154._crit_edge

Flow154._crit_edge:                               ; preds = %Flow154
  br label %1044

LeafBlock80:                                      ; preds = %Flow154
  %SwitchLeaf81 = icmp eq i32 %1001, 0
  br i1 %SwitchLeaf81, label %1027, label %LeafBlock80.Flow153_crit_edge

LeafBlock80.Flow153_crit_edge:                    ; preds = %LeafBlock80
  br label %Flow153

1005:                                             ; preds = %LeafBlock82
  %1006 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1007 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1008 = sext i32 %1006 to i64
  %1009 = sext i32 %1007 to i64
  %.split70 = getelementptr [16 x i8], ptr %1, i64 %1008
  %1010 = getelementptr [4 x i8], ptr %.split70, i64 %1009
  %1011 = load float, ptr %1010, align 4
  %1012 = sext i32 %982 to i64
  %1013 = getelementptr [4 x i8], ptr %2, i64 %1012
  %1014 = load float, ptr %1013, align 4
  %1015 = call i1 @llvm.is.fpclass.f32(float %1014, /* (sub) */ i32 144)
  %1016 = call float @llvm.copysign.f32(float 0.000000e+00, float %1014)
  %1017 = select i1 %1015, float %1016, float %1014
  %1018 = call i1 @llvm.is.fpclass.f32(float %1011, /* (sub) */ i32 144)
  %1019 = call float @llvm.copysign.f32(float 0.000000e+00, float %1011)
  %1020 = select i1 %1018, float %1019, float %1011
  %1021 = fadd float %1017, %1020
  %1022 = call i1 @llvm.is.fpclass.f32(float %1021, /* (sub) */ i32 144)
  %1023 = call float @llvm.copysign.f32(float 0.000000e+00, float %1021)
  %1024 = select i1 %1022, float %1023, float %1021
  %1025 = sext i32 %982 to i64
  %1026 = getelementptr [4 x i8], ptr %2, i64 %1025
  store float %1024, ptr %1026, align 4
  br label %Flow152

1027:                                             ; preds = %LeafBlock80
  %1028 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %1029 = sext i32 %982 to i64
  %1030 = getelementptr [4 x i8], ptr %2, i64 %1029
  %1031 = load float, ptr %1030, align 4
  %1032 = call i1 @llvm.is.fpclass.f32(float %1031, /* (sub) */ i32 144)
  %1033 = call float @llvm.copysign.f32(float 0.000000e+00, float %1031)
  %1034 = select i1 %1032, float %1033, float %1031
  %1035 = call i1 @llvm.is.fpclass.f32(float %1028, /* (sub) */ i32 144)
  %1036 = call float @llvm.copysign.f32(float 0.000000e+00, float %1028)
  %1037 = select i1 %1035, float %1036, float %1028
  %1038 = fadd float %1034, %1037
  %1039 = call i1 @llvm.is.fpclass.f32(float %1038, /* (sub) */ i32 144)
  %1040 = call float @llvm.copysign.f32(float 0.000000e+00, float %1038)
  %1041 = select i1 %1039, float %1040, float %1038
  %1042 = sext i32 %982 to i64
  %1043 = getelementptr [4 x i8], ptr %2, i64 %1042
  store float %1041, ptr %1043, align 4
  br label %Flow153

Flow152:                                          ; preds = %LeafBlock82.Flow152_crit_edge, %1005
  br label %Flow154

Flow153:                                          ; preds = %LeafBlock80.Flow153_crit_edge, %1027
  br label %1044

1044:                                             ; preds = %Flow154._crit_edge, %Flow153
  %1045 = add i32 %.0.3.3, 1
  br label %Flow155

1046:                                             ; preds = %Flow157
  %1047 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1048 = insertelement <4 x float> poison, float %1047, i64 0
  %1049 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1050 = insertelement <4 x float> %1048, float %1049, i64 1
  %1051 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1052 = insertelement <4 x float> %1050, float %1051, i64 2
  %1053 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1054 = insertelement <4 x float> %1052, float %1053, i64 3
  %1055 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1056 = fcmp uge float %1047, %1055
  br i1 %1056, label %1057, label %.Flow151_crit_edge

.Flow151_crit_edge:                               ; preds = %1046
  br label %Flow151

1057:                                             ; preds = %1046
  %1058 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1059 = call i32 @llvm.smin.i32(i32 %986, i32 %1058)
  %1060 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1061 = sext i32 %1059 to i64
  %1062 = getelementptr [4 x i8], ptr %2, i64 %1061
  store float %1060, ptr %1062, align 4
  br label %1064

Flow151:                                          ; preds = %.Flow151_crit_edge, %1066
  %1063 = phi i32 [ %1067, %1066 ], [ %986, %.Flow151_crit_edge ]
  br label %1123

1064:                                             ; preds = %Flow150._crit_edge, %1057
  %.034.458 = phi i32 [ %1070, %Flow150._crit_edge ], [ 0, %1057 ]
  %1065 = icmp slt i32 %.034.458, 4
  br i1 %1065, label %1068, label %.Flow150_crit_edge

.Flow150_crit_edge:                               ; preds = %1064
  br label %Flow150

1066:                                             ; preds = %Flow150
  %1067 = add i32 %986, 1
  br label %Flow151

1068:                                             ; preds = %1064
  %1069 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %1072

Flow150:                                          ; preds = %.Flow150_crit_edge, %1074
  %1070 = phi i32 [ %1075, %1074 ], [ poison, %.Flow150_crit_edge ]
  %1071 = phi i1 [ false, %1074 ], [ true, %.Flow150_crit_edge ]
  br i1 %1071, label %1066, label %Flow150._crit_edge

Flow150._crit_edge:                               ; preds = %Flow150
  br label %1064

1072:                                             ; preds = %Flow149._crit_edge, %1068
  %.0.459 = phi i32 [ %1079, %Flow149._crit_edge ], [ %1069, %1068 ]
  %1073 = icmp slt i32 %.0.459, 1
  br i1 %1073, label %1076, label %.Flow149_crit_edge

.Flow149_crit_edge:                               ; preds = %1072
  br label %Flow149

1074:                                             ; preds = %Flow149
  %1075 = add i32 %.034.458, 1
  br label %Flow150

1076:                                             ; preds = %1072
  %1077 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %1078 = sub i32 %1059, %1077
  br label %NodeBlock90

Flow149:                                          ; preds = %.Flow149_crit_edge, %1121
  %1079 = phi i32 [ %1122, %1121 ], [ poison, %.Flow149_crit_edge ]
  %1080 = phi i1 [ false, %1121 ], [ true, %.Flow149_crit_edge ]
  br i1 %1080, label %1074, label %Flow149._crit_edge

Flow149._crit_edge:                               ; preds = %Flow149
  br label %1072

NodeBlock90:                                      ; preds = %1076
  %Pivot91 = icmp sge i32 %1078, 2
  br i1 %Pivot91, label %LeafBlock88, label %NodeBlock90.Flow148_crit_edge

NodeBlock90.Flow148_crit_edge:                    ; preds = %NodeBlock90
  br label %Flow148

LeafBlock88:                                      ; preds = %NodeBlock90
  %SwitchLeaf89 = icmp eq i32 %1078, 2
  br i1 %SwitchLeaf89, label %1082, label %LeafBlock88.Flow146_crit_edge

LeafBlock88.Flow146_crit_edge:                    ; preds = %LeafBlock88
  br label %Flow146

Flow148:                                          ; preds = %NodeBlock90.Flow148_crit_edge, %Flow146
  %1081 = phi i1 [ false, %Flow146 ], [ true, %NodeBlock90.Flow148_crit_edge ]
  br i1 %1081, label %LeafBlock86, label %Flow148._crit_edge

Flow148._crit_edge:                               ; preds = %Flow148
  br label %1121

LeafBlock86:                                      ; preds = %Flow148
  %SwitchLeaf87 = icmp eq i32 %1078, 0
  br i1 %SwitchLeaf87, label %1104, label %LeafBlock86.Flow147_crit_edge

LeafBlock86.Flow147_crit_edge:                    ; preds = %LeafBlock86
  br label %Flow147

1082:                                             ; preds = %LeafBlock88
  %1083 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1084 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1085 = sext i32 %1083 to i64
  %1086 = sext i32 %1084 to i64
  %.split71 = getelementptr [16 x i8], ptr %1, i64 %1085
  %1087 = getelementptr [4 x i8], ptr %.split71, i64 %1086
  %1088 = load float, ptr %1087, align 4
  %1089 = sext i32 %1059 to i64
  %1090 = getelementptr [4 x i8], ptr %2, i64 %1089
  %1091 = load float, ptr %1090, align 4
  %1092 = call i1 @llvm.is.fpclass.f32(float %1091, /* (sub) */ i32 144)
  %1093 = call float @llvm.copysign.f32(float 0.000000e+00, float %1091)
  %1094 = select i1 %1092, float %1093, float %1091
  %1095 = call i1 @llvm.is.fpclass.f32(float %1088, /* (sub) */ i32 144)
  %1096 = call float @llvm.copysign.f32(float 0.000000e+00, float %1088)
  %1097 = select i1 %1095, float %1096, float %1088
  %1098 = fadd float %1094, %1097
  %1099 = call i1 @llvm.is.fpclass.f32(float %1098, /* (sub) */ i32 144)
  %1100 = call float @llvm.copysign.f32(float 0.000000e+00, float %1098)
  %1101 = select i1 %1099, float %1100, float %1098
  %1102 = sext i32 %1059 to i64
  %1103 = getelementptr [4 x i8], ptr %2, i64 %1102
  store float %1101, ptr %1103, align 4
  br label %Flow146

1104:                                             ; preds = %LeafBlock86
  %1105 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %1106 = sext i32 %1059 to i64
  %1107 = getelementptr [4 x i8], ptr %2, i64 %1106
  %1108 = load float, ptr %1107, align 4
  %1109 = call i1 @llvm.is.fpclass.f32(float %1108, /* (sub) */ i32 144)
  %1110 = call float @llvm.copysign.f32(float 0.000000e+00, float %1108)
  %1111 = select i1 %1109, float %1110, float %1108
  %1112 = call i1 @llvm.is.fpclass.f32(float %1105, /* (sub) */ i32 144)
  %1113 = call float @llvm.copysign.f32(float 0.000000e+00, float %1105)
  %1114 = select i1 %1112, float %1113, float %1105
  %1115 = fadd float %1111, %1114
  %1116 = call i1 @llvm.is.fpclass.f32(float %1115, /* (sub) */ i32 144)
  %1117 = call float @llvm.copysign.f32(float 0.000000e+00, float %1115)
  %1118 = select i1 %1116, float %1117, float %1115
  %1119 = sext i32 %1059 to i64
  %1120 = getelementptr [4 x i8], ptr %2, i64 %1119
  store float %1118, ptr %1120, align 4
  br label %Flow147

Flow146:                                          ; preds = %LeafBlock88.Flow146_crit_edge, %1082
  br label %Flow148

Flow147:                                          ; preds = %LeafBlock86.Flow147_crit_edge, %1104
  br label %1121

1121:                                             ; preds = %Flow148._crit_edge, %Flow147
  %1122 = add i32 %.0.459, 1
  br label %Flow149

1123:                                             ; preds = %Flow151
  %1124 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1125 = insertelement <4 x float> poison, float %1124, i64 0
  %1126 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1127 = insertelement <4 x float> %1125, float %1126, i64 1
  %1128 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1129 = insertelement <4 x float> %1127, float %1128, i64 2
  %1130 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1131 = insertelement <4 x float> %1129, float %1130, i64 3
  %1132 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1133 = fcmp uge float %1124, %1132
  br i1 %1133, label %1134, label %.Flow145_crit_edge

.Flow145_crit_edge:                               ; preds = %1123
  br label %Flow145

1134:                                             ; preds = %1123
  %1135 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1136 = call i32 @llvm.smin.i32(i32 %1063, i32 %1135)
  %1137 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1138 = sext i32 %1136 to i64
  %1139 = getelementptr [4 x i8], ptr %2, i64 %1138
  store float %1137, ptr %1139, align 4
  br label %1141

Flow145:                                          ; preds = %.Flow145_crit_edge, %1143
  %1140 = phi i32 [ %1144, %1143 ], [ %1063, %.Flow145_crit_edge ]
  br label %1200

1141:                                             ; preds = %Flow144._crit_edge, %1134
  %.034.1.4 = phi i32 [ %1147, %Flow144._crit_edge ], [ 0, %1134 ]
  %1142 = icmp slt i32 %.034.1.4, 4
  br i1 %1142, label %1145, label %.Flow144_crit_edge

.Flow144_crit_edge:                               ; preds = %1141
  br label %Flow144

1143:                                             ; preds = %Flow144
  %1144 = add i32 %1063, 1
  br label %Flow145

1145:                                             ; preds = %1141
  %1146 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %1149

Flow144:                                          ; preds = %.Flow144_crit_edge, %1151
  %1147 = phi i32 [ %1152, %1151 ], [ poison, %.Flow144_crit_edge ]
  %1148 = phi i1 [ false, %1151 ], [ true, %.Flow144_crit_edge ]
  br i1 %1148, label %1143, label %Flow144._crit_edge

Flow144._crit_edge:                               ; preds = %Flow144
  br label %1141

1149:                                             ; preds = %Flow143._crit_edge, %1145
  %.0.1.4 = phi i32 [ %1156, %Flow143._crit_edge ], [ %1146, %1145 ]
  %1150 = icmp slt i32 %.0.1.4, 2
  br i1 %1150, label %1153, label %.Flow143_crit_edge

.Flow143_crit_edge:                               ; preds = %1149
  br label %Flow143

1151:                                             ; preds = %Flow143
  %1152 = add i32 %.034.1.4, 1
  br label %Flow144

1153:                                             ; preds = %1149
  %1154 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %1155 = sub i32 %1136, %1154
  br label %NodeBlock96

Flow143:                                          ; preds = %.Flow143_crit_edge, %1198
  %1156 = phi i32 [ %1199, %1198 ], [ poison, %.Flow143_crit_edge ]
  %1157 = phi i1 [ false, %1198 ], [ true, %.Flow143_crit_edge ]
  br i1 %1157, label %1151, label %Flow143._crit_edge

Flow143._crit_edge:                               ; preds = %Flow143
  br label %1149

NodeBlock96:                                      ; preds = %1153
  %Pivot97 = icmp sge i32 %1155, 2
  br i1 %Pivot97, label %LeafBlock94, label %NodeBlock96.Flow142_crit_edge

NodeBlock96.Flow142_crit_edge:                    ; preds = %NodeBlock96
  br label %Flow142

LeafBlock94:                                      ; preds = %NodeBlock96
  %SwitchLeaf95 = icmp eq i32 %1155, 2
  br i1 %SwitchLeaf95, label %1159, label %LeafBlock94.Flow140_crit_edge

LeafBlock94.Flow140_crit_edge:                    ; preds = %LeafBlock94
  br label %Flow140

Flow142:                                          ; preds = %NodeBlock96.Flow142_crit_edge, %Flow140
  %1158 = phi i1 [ false, %Flow140 ], [ true, %NodeBlock96.Flow142_crit_edge ]
  br i1 %1158, label %LeafBlock92, label %Flow142._crit_edge

Flow142._crit_edge:                               ; preds = %Flow142
  br label %1198

LeafBlock92:                                      ; preds = %Flow142
  %SwitchLeaf93 = icmp eq i32 %1155, 0
  br i1 %SwitchLeaf93, label %1181, label %LeafBlock92.Flow141_crit_edge

LeafBlock92.Flow141_crit_edge:                    ; preds = %LeafBlock92
  br label %Flow141

1159:                                             ; preds = %LeafBlock94
  %1160 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1161 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1162 = sext i32 %1160 to i64
  %1163 = sext i32 %1161 to i64
  %.split72 = getelementptr [16 x i8], ptr %1, i64 %1162
  %1164 = getelementptr [4 x i8], ptr %.split72, i64 %1163
  %1165 = load float, ptr %1164, align 4
  %1166 = sext i32 %1136 to i64
  %1167 = getelementptr [4 x i8], ptr %2, i64 %1166
  %1168 = load float, ptr %1167, align 4
  %1169 = call i1 @llvm.is.fpclass.f32(float %1168, /* (sub) */ i32 144)
  %1170 = call float @llvm.copysign.f32(float 0.000000e+00, float %1168)
  %1171 = select i1 %1169, float %1170, float %1168
  %1172 = call i1 @llvm.is.fpclass.f32(float %1165, /* (sub) */ i32 144)
  %1173 = call float @llvm.copysign.f32(float 0.000000e+00, float %1165)
  %1174 = select i1 %1172, float %1173, float %1165
  %1175 = fadd float %1171, %1174
  %1176 = call i1 @llvm.is.fpclass.f32(float %1175, /* (sub) */ i32 144)
  %1177 = call float @llvm.copysign.f32(float 0.000000e+00, float %1175)
  %1178 = select i1 %1176, float %1177, float %1175
  %1179 = sext i32 %1136 to i64
  %1180 = getelementptr [4 x i8], ptr %2, i64 %1179
  store float %1178, ptr %1180, align 4
  br label %Flow140

1181:                                             ; preds = %LeafBlock92
  %1182 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %1183 = sext i32 %1136 to i64
  %1184 = getelementptr [4 x i8], ptr %2, i64 %1183
  %1185 = load float, ptr %1184, align 4
  %1186 = call i1 @llvm.is.fpclass.f32(float %1185, /* (sub) */ i32 144)
  %1187 = call float @llvm.copysign.f32(float 0.000000e+00, float %1185)
  %1188 = select i1 %1186, float %1187, float %1185
  %1189 = call i1 @llvm.is.fpclass.f32(float %1182, /* (sub) */ i32 144)
  %1190 = call float @llvm.copysign.f32(float 0.000000e+00, float %1182)
  %1191 = select i1 %1189, float %1190, float %1182
  %1192 = fadd float %1188, %1191
  %1193 = call i1 @llvm.is.fpclass.f32(float %1192, /* (sub) */ i32 144)
  %1194 = call float @llvm.copysign.f32(float 0.000000e+00, float %1192)
  %1195 = select i1 %1193, float %1194, float %1192
  %1196 = sext i32 %1136 to i64
  %1197 = getelementptr [4 x i8], ptr %2, i64 %1196
  store float %1195, ptr %1197, align 4
  br label %Flow141

Flow140:                                          ; preds = %LeafBlock94.Flow140_crit_edge, %1159
  br label %Flow142

Flow141:                                          ; preds = %LeafBlock92.Flow141_crit_edge, %1181
  br label %1198

1198:                                             ; preds = %Flow142._crit_edge, %Flow141
  %1199 = add i32 %.0.1.4, 1
  br label %Flow143

1200:                                             ; preds = %Flow145
  %1201 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1202 = insertelement <4 x float> poison, float %1201, i64 0
  %1203 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1204 = insertelement <4 x float> %1202, float %1203, i64 1
  %1205 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1206 = insertelement <4 x float> %1204, float %1205, i64 2
  %1207 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1208 = insertelement <4 x float> %1206, float %1207, i64 3
  %1209 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1210 = fcmp uge float %1201, %1209
  br i1 %1210, label %1211, label %.Flow139_crit_edge

.Flow139_crit_edge:                               ; preds = %1200
  br label %Flow139

1211:                                             ; preds = %1200
  %1212 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1213 = call i32 @llvm.smin.i32(i32 %1140, i32 %1212)
  %1214 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1215 = sext i32 %1213 to i64
  %1216 = getelementptr [4 x i8], ptr %2, i64 %1215
  store float %1214, ptr %1216, align 4
  br label %1218

Flow139:                                          ; preds = %.Flow139_crit_edge, %1220
  %1217 = phi i32 [ %1221, %1220 ], [ %1140, %.Flow139_crit_edge ]
  br label %1277

1218:                                             ; preds = %Flow138._crit_edge, %1211
  %.034.2.4 = phi i32 [ %1224, %Flow138._crit_edge ], [ 0, %1211 ]
  %1219 = icmp slt i32 %.034.2.4, 4
  br i1 %1219, label %1222, label %.Flow138_crit_edge

.Flow138_crit_edge:                               ; preds = %1218
  br label %Flow138

1220:                                             ; preds = %Flow138
  %1221 = add i32 %1140, 1
  br label %Flow139

1222:                                             ; preds = %1218
  %1223 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %1226

Flow138:                                          ; preds = %.Flow138_crit_edge, %1228
  %1224 = phi i32 [ %1229, %1228 ], [ poison, %.Flow138_crit_edge ]
  %1225 = phi i1 [ false, %1228 ], [ true, %.Flow138_crit_edge ]
  br i1 %1225, label %1220, label %Flow138._crit_edge

Flow138._crit_edge:                               ; preds = %Flow138
  br label %1218

1226:                                             ; preds = %Flow137._crit_edge, %1222
  %.0.2.4 = phi i32 [ %1233, %Flow137._crit_edge ], [ %1223, %1222 ]
  %1227 = icmp slt i32 %.0.2.4, 3
  br i1 %1227, label %1230, label %.Flow137_crit_edge

.Flow137_crit_edge:                               ; preds = %1226
  br label %Flow137

1228:                                             ; preds = %Flow137
  %1229 = add i32 %.034.2.4, 1
  br label %Flow138

1230:                                             ; preds = %1226
  %1231 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %1232 = sub i32 %1213, %1231
  br label %NodeBlock102

Flow137:                                          ; preds = %.Flow137_crit_edge, %1275
  %1233 = phi i32 [ %1276, %1275 ], [ poison, %.Flow137_crit_edge ]
  %1234 = phi i1 [ false, %1275 ], [ true, %.Flow137_crit_edge ]
  br i1 %1234, label %1228, label %Flow137._crit_edge

Flow137._crit_edge:                               ; preds = %Flow137
  br label %1226

NodeBlock102:                                     ; preds = %1230
  %Pivot103 = icmp sge i32 %1232, 2
  br i1 %Pivot103, label %LeafBlock100, label %NodeBlock102.Flow136_crit_edge

NodeBlock102.Flow136_crit_edge:                   ; preds = %NodeBlock102
  br label %Flow136

LeafBlock100:                                     ; preds = %NodeBlock102
  %SwitchLeaf101 = icmp eq i32 %1232, 2
  br i1 %SwitchLeaf101, label %1236, label %LeafBlock100.Flow134_crit_edge

LeafBlock100.Flow134_crit_edge:                   ; preds = %LeafBlock100
  br label %Flow134

Flow136:                                          ; preds = %NodeBlock102.Flow136_crit_edge, %Flow134
  %1235 = phi i1 [ false, %Flow134 ], [ true, %NodeBlock102.Flow136_crit_edge ]
  br i1 %1235, label %LeafBlock98, label %Flow136._crit_edge

Flow136._crit_edge:                               ; preds = %Flow136
  br label %1275

LeafBlock98:                                      ; preds = %Flow136
  %SwitchLeaf99 = icmp eq i32 %1232, 0
  br i1 %SwitchLeaf99, label %1258, label %LeafBlock98.Flow135_crit_edge

LeafBlock98.Flow135_crit_edge:                    ; preds = %LeafBlock98
  br label %Flow135

1236:                                             ; preds = %LeafBlock100
  %1237 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1238 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1239 = sext i32 %1237 to i64
  %1240 = sext i32 %1238 to i64
  %.split73 = getelementptr [16 x i8], ptr %1, i64 %1239
  %1241 = getelementptr [4 x i8], ptr %.split73, i64 %1240
  %1242 = load float, ptr %1241, align 4
  %1243 = sext i32 %1213 to i64
  %1244 = getelementptr [4 x i8], ptr %2, i64 %1243
  %1245 = load float, ptr %1244, align 4
  %1246 = call i1 @llvm.is.fpclass.f32(float %1245, /* (sub) */ i32 144)
  %1247 = call float @llvm.copysign.f32(float 0.000000e+00, float %1245)
  %1248 = select i1 %1246, float %1247, float %1245
  %1249 = call i1 @llvm.is.fpclass.f32(float %1242, /* (sub) */ i32 144)
  %1250 = call float @llvm.copysign.f32(float 0.000000e+00, float %1242)
  %1251 = select i1 %1249, float %1250, float %1242
  %1252 = fadd float %1248, %1251
  %1253 = call i1 @llvm.is.fpclass.f32(float %1252, /* (sub) */ i32 144)
  %1254 = call float @llvm.copysign.f32(float 0.000000e+00, float %1252)
  %1255 = select i1 %1253, float %1254, float %1252
  %1256 = sext i32 %1213 to i64
  %1257 = getelementptr [4 x i8], ptr %2, i64 %1256
  store float %1255, ptr %1257, align 4
  br label %Flow134

1258:                                             ; preds = %LeafBlock98
  %1259 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %1260 = sext i32 %1213 to i64
  %1261 = getelementptr [4 x i8], ptr %2, i64 %1260
  %1262 = load float, ptr %1261, align 4
  %1263 = call i1 @llvm.is.fpclass.f32(float %1262, /* (sub) */ i32 144)
  %1264 = call float @llvm.copysign.f32(float 0.000000e+00, float %1262)
  %1265 = select i1 %1263, float %1264, float %1262
  %1266 = call i1 @llvm.is.fpclass.f32(float %1259, /* (sub) */ i32 144)
  %1267 = call float @llvm.copysign.f32(float 0.000000e+00, float %1259)
  %1268 = select i1 %1266, float %1267, float %1259
  %1269 = fadd float %1265, %1268
  %1270 = call i1 @llvm.is.fpclass.f32(float %1269, /* (sub) */ i32 144)
  %1271 = call float @llvm.copysign.f32(float 0.000000e+00, float %1269)
  %1272 = select i1 %1270, float %1271, float %1269
  %1273 = sext i32 %1213 to i64
  %1274 = getelementptr [4 x i8], ptr %2, i64 %1273
  store float %1272, ptr %1274, align 4
  br label %Flow135

Flow134:                                          ; preds = %LeafBlock100.Flow134_crit_edge, %1236
  br label %Flow136

Flow135:                                          ; preds = %LeafBlock98.Flow135_crit_edge, %1258
  br label %1275

1275:                                             ; preds = %Flow136._crit_edge, %Flow135
  %1276 = add i32 %.0.2.4, 1
  br label %Flow137

1277:                                             ; preds = %Flow139
  %1278 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1279 = insertelement <4 x float> poison, float %1278, i64 0
  %1280 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1281 = insertelement <4 x float> %1279, float %1280, i64 1
  %1282 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1283 = insertelement <4 x float> %1281, float %1282, i64 2
  %1284 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1285 = insertelement <4 x float> %1283, float %1284, i64 3
  %1286 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1287 = fcmp uge float %1278, %1286
  br i1 %1287, label %1288, label %.Flow133_crit_edge

.Flow133_crit_edge:                               ; preds = %1277
  br label %Flow133

1288:                                             ; preds = %1277
  %1289 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1290 = call i32 @llvm.smin.i32(i32 %1217, i32 %1289)
  %1291 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1292 = sext i32 %1290 to i64
  %1293 = getelementptr [4 x i8], ptr %2, i64 %1292
  store float %1291, ptr %1293, align 4
  br label %1294

Flow133:                                          ; preds = %.Flow133_crit_edge, %Flow132
  br label %1351

1294:                                             ; preds = %Flow131._crit_edge, %1288
  %.034.3.4 = phi i32 [ %1298, %Flow131._crit_edge ], [ 0, %1288 ]
  %1295 = icmp slt i32 %.034.3.4, 4
  br i1 %1295, label %1296, label %.Flow131_crit_edge

.Flow131_crit_edge:                               ; preds = %1294
  br label %Flow131

1296:                                             ; preds = %1294
  %1297 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %1300

Flow131:                                          ; preds = %.Flow131_crit_edge, %1302
  %1298 = phi i32 [ %1303, %1302 ], [ poison, %.Flow131_crit_edge ]
  %1299 = phi i1 [ false, %1302 ], [ true, %.Flow131_crit_edge ]
  br i1 %1299, label %Flow132, label %Flow131._crit_edge

Flow131._crit_edge:                               ; preds = %Flow131
  br label %1294

1300:                                             ; preds = %Flow130._crit_edge, %1296
  %.0.3.4 = phi i32 [ %1307, %Flow130._crit_edge ], [ %1297, %1296 ]
  %1301 = icmp slt i32 %.0.3.4, 4
  br i1 %1301, label %1304, label %.Flow130_crit_edge

.Flow130_crit_edge:                               ; preds = %1300
  br label %Flow130

1302:                                             ; preds = %Flow130
  %1303 = add i32 %.034.3.4, 1
  br label %Flow131

1304:                                             ; preds = %1300
  %1305 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 64, i1 true)
  %1306 = sub i32 %1290, %1305
  br label %NodeBlock108

Flow130:                                          ; preds = %.Flow130_crit_edge, %1349
  %1307 = phi i32 [ %1350, %1349 ], [ poison, %.Flow130_crit_edge ]
  %1308 = phi i1 [ false, %1349 ], [ true, %.Flow130_crit_edge ]
  br i1 %1308, label %1302, label %Flow130._crit_edge

Flow130._crit_edge:                               ; preds = %Flow130
  br label %1300

NodeBlock108:                                     ; preds = %1304
  %Pivot109 = icmp sge i32 %1306, 2
  br i1 %Pivot109, label %LeafBlock106, label %NodeBlock108.Flow129_crit_edge

NodeBlock108.Flow129_crit_edge:                   ; preds = %NodeBlock108
  br label %Flow129

LeafBlock106:                                     ; preds = %NodeBlock108
  %SwitchLeaf107 = icmp eq i32 %1306, 2
  br i1 %SwitchLeaf107, label %1310, label %LeafBlock106.Flow127_crit_edge

LeafBlock106.Flow127_crit_edge:                   ; preds = %LeafBlock106
  br label %Flow127

Flow129:                                          ; preds = %NodeBlock108.Flow129_crit_edge, %Flow127
  %1309 = phi i1 [ false, %Flow127 ], [ true, %NodeBlock108.Flow129_crit_edge ]
  br i1 %1309, label %LeafBlock104, label %Flow129._crit_edge

Flow129._crit_edge:                               ; preds = %Flow129
  br label %1349

LeafBlock104:                                     ; preds = %Flow129
  %SwitchLeaf105 = icmp eq i32 %1306, 0
  br i1 %SwitchLeaf105, label %1332, label %LeafBlock104.Flow128_crit_edge

LeafBlock104.Flow128_crit_edge:                   ; preds = %LeafBlock104
  br label %Flow128

1310:                                             ; preds = %LeafBlock106
  %1311 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1312 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %1313 = sext i32 %1311 to i64
  %1314 = sext i32 %1312 to i64
  %.split74 = getelementptr [16 x i8], ptr %1, i64 %1313
  %1315 = getelementptr [4 x i8], ptr %.split74, i64 %1314
  %1316 = load float, ptr %1315, align 4
  %1317 = sext i32 %1290 to i64
  %1318 = getelementptr [4 x i8], ptr %2, i64 %1317
  %1319 = load float, ptr %1318, align 4
  %1320 = call i1 @llvm.is.fpclass.f32(float %1319, /* (sub) */ i32 144)
  %1321 = call float @llvm.copysign.f32(float 0.000000e+00, float %1319)
  %1322 = select i1 %1320, float %1321, float %1319
  %1323 = call i1 @llvm.is.fpclass.f32(float %1316, /* (sub) */ i32 144)
  %1324 = call float @llvm.copysign.f32(float 0.000000e+00, float %1316)
  %1325 = select i1 %1323, float %1324, float %1316
  %1326 = fadd float %1322, %1325
  %1327 = call i1 @llvm.is.fpclass.f32(float %1326, /* (sub) */ i32 144)
  %1328 = call float @llvm.copysign.f32(float 0.000000e+00, float %1326)
  %1329 = select i1 %1327, float %1328, float %1326
  %1330 = sext i32 %1290 to i64
  %1331 = getelementptr [4 x i8], ptr %2, i64 %1330
  store float %1329, ptr %1331, align 4
  br label %Flow127

1332:                                             ; preds = %LeafBlock104
  %1333 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 96, i1 true)
  %1334 = sext i32 %1290 to i64
  %1335 = getelementptr [4 x i8], ptr %2, i64 %1334
  %1336 = load float, ptr %1335, align 4
  %1337 = call i1 @llvm.is.fpclass.f32(float %1336, /* (sub) */ i32 144)
  %1338 = call float @llvm.copysign.f32(float 0.000000e+00, float %1336)
  %1339 = select i1 %1337, float %1338, float %1336
  %1340 = call i1 @llvm.is.fpclass.f32(float %1333, /* (sub) */ i32 144)
  %1341 = call float @llvm.copysign.f32(float 0.000000e+00, float %1333)
  %1342 = select i1 %1340, float %1341, float %1333
  %1343 = fadd float %1339, %1342
  %1344 = call i1 @llvm.is.fpclass.f32(float %1343, /* (sub) */ i32 144)
  %1345 = call float @llvm.copysign.f32(float 0.000000e+00, float %1343)
  %1346 = select i1 %1344, float %1345, float %1343
  %1347 = sext i32 %1290 to i64
  %1348 = getelementptr [4 x i8], ptr %2, i64 %1347
  store float %1346, ptr %1348, align 4
  br label %Flow128

Flow127:                                          ; preds = %LeafBlock106.Flow127_crit_edge, %1310
  br label %Flow129

Flow128:                                          ; preds = %LeafBlock104.Flow128_crit_edge, %1332
  br label %1349

1349:                                             ; preds = %Flow129._crit_edge, %Flow128
  %1350 = add i32 %.0.3.4, 1
  br label %Flow130

Flow132:                                          ; preds = %Flow131
  br label %Flow133

1351:                                             ; preds = %Flow133
  %1352 = load float, ptr %2, align 4
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %1352, i32 0)
  br label %Flow225

Flow225:                                          ; preds = %Flow224.Flow225_crit_edge, %1351
  br label %common.ret

1353:                                             ; preds = %._crit_edge243, %1418
  %.2 = phi i32 [ %1424, %1418 ], [ %34, %._crit_edge243 ]
  %1354 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1355 = insertelement <4 x float> poison, float %1354, i64 0
  %1356 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1357 = insertelement <4 x float> %1355, float %1356, i64 1
  %1358 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1359 = insertelement <4 x float> %1357, float %1358, i64 2
  %1360 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1361 = insertelement <4 x float> %1359, float %1360, i64 3
  %1362 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1363 = fcmp uge float %1354, %1362
  br i1 %1363, label %1364, label %._crit_edge245

._crit_edge245:                                   ; preds = %1353
  br label %1371

1364:                                             ; preds = %1353
  %1365 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1366 = call i32 @llvm.smin.i32(i32 %.2, i32 %1365)
  %1367 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1368 = sext i32 %1366 to i64
  %1369 = getelementptr [4 x i8], ptr %2, i64 %1368
  store float %1367, ptr %1369, align 4
  %1370 = add i32 %.2, 1
  br label %1371

1371:                                             ; preds = %._crit_edge245, %1364
  %.2.1 = phi i32 [ %1370, %1364 ], [ %.2, %._crit_edge245 ]
  %1372 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1373 = insertelement <4 x float> poison, float %1372, i64 0
  %1374 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1375 = insertelement <4 x float> %1373, float %1374, i64 1
  %1376 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1377 = insertelement <4 x float> %1375, float %1376, i64 2
  %1378 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1379 = insertelement <4 x float> %1377, float %1378, i64 3
  %1380 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1381 = fcmp uge float %1372, %1380
  br i1 %1381, label %1382, label %._crit_edge246

._crit_edge246:                                   ; preds = %1371
  br label %1389

1382:                                             ; preds = %1371
  %1383 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1384 = call i32 @llvm.smin.i32(i32 %.2.1, i32 %1383)
  %1385 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1386 = sext i32 %1384 to i64
  %1387 = getelementptr [4 x i8], ptr %2, i64 %1386
  store float %1385, ptr %1387, align 4
  %1388 = add i32 %.2.1, 1
  br label %1389

1389:                                             ; preds = %._crit_edge246, %1382
  %.2.2 = phi i32 [ %1388, %1382 ], [ %.2.1, %._crit_edge246 ]
  %1390 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1391 = insertelement <4 x float> poison, float %1390, i64 0
  %1392 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1393 = insertelement <4 x float> %1391, float %1392, i64 1
  %1394 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1395 = insertelement <4 x float> %1393, float %1394, i64 2
  %1396 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1397 = insertelement <4 x float> %1395, float %1396, i64 3
  %1398 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1399 = fcmp uge float %1390, %1398
  br i1 %1399, label %1400, label %._crit_edge247

._crit_edge247:                                   ; preds = %1389
  br label %1407

1400:                                             ; preds = %1389
  %1401 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1402 = call i32 @llvm.smin.i32(i32 %.2.2, i32 %1401)
  %1403 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1404 = sext i32 %1402 to i64
  %1405 = getelementptr [4 x i8], ptr %2, i64 %1404
  store float %1403, ptr %1405, align 4
  %1406 = add i32 %.2.2, 1
  br label %1407

1407:                                             ; preds = %._crit_edge247, %1400
  %.2.3 = phi i32 [ %1406, %1400 ], [ %.2.2, %._crit_edge247 ]
  %1408 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %1409 = insertelement <4 x float> poison, float %1408, i64 0
  %1410 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %1411 = insertelement <4 x float> %1409, float %1410, i64 1
  %1412 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %1413 = insertelement <4 x float> %1411, float %1412, i64 2
  %1414 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %1415 = insertelement <4 x float> %1413, float %1414, i64 3
  %1416 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 160, i1 true)
  %1417 = fcmp uge float %1408, %1416
  br i1 %1417, label %133, label %.Flow223_crit_edge

.Flow223_crit_edge:                               ; preds = %1407
  br label %Flow223

1418:                                             ; preds = %24
  %1419 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %1420 = call i32 @llvm.smin.i32(i32 %34, i32 %1419)
  %1421 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %1422 = sext i32 %1420 to i64
  %1423 = getelementptr [4 x i8], ptr %2, i64 %1422
  store float %1421, ptr %1423, align 4
  %1424 = add i32 %34, 1
  br label %1353

Flow242:                                          ; preds = %loop.exit.guard116.Flow242_crit_edge, %47
  %1425 = phi i32 [ %48, %47 ], [ poison, %loop.exit.guard116.Flow242_crit_edge ]
  %1426 = phi i1 [ false, %47 ], [ true, %loop.exit.guard116.Flow242_crit_edge ]
  br label %Flow241

loop.exit.guard:                                  ; preds = %Flow241
  %Guard..inv = xor i1 %1435, true
  %Guard.113.inv = xor i1 %1434, true
  %Guard.114.inv = xor i1 %1433, true
  %Guard.115.inv = xor i1 %1432, true
  br i1 %Guard..inv, label %loop.exit.guard110, label %loop.exit.guard.Flow224_crit_edge

loop.exit.guard.Flow224_crit_edge:                ; preds = %loop.exit.guard
  br label %Flow224

loop.exit.guard110:                               ; preds = %loop.exit.guard
  br i1 %Guard.113.inv, label %loop.exit.guard111, label %loop.exit.guard110.Flow125_crit_edge

loop.exit.guard110.Flow125_crit_edge:             ; preds = %loop.exit.guard110
  br label %Flow125

loop.exit.guard111:                               ; preds = %loop.exit.guard110
  br i1 %Guard.114.inv, label %loop.exit.guard112, label %loop.exit.guard111.Flow123_crit_edge

loop.exit.guard111.Flow123_crit_edge:             ; preds = %loop.exit.guard111
  br label %Flow123

loop.exit.guard112:                               ; preds = %loop.exit.guard111
  br i1 %Guard.115.inv, label %112, label %loop.exit.guard112.Flow_crit_edge

loop.exit.guard112.Flow_crit_edge:                ; preds = %loop.exit.guard112
  br label %Flow

Flow240:                                          ; preds = %loop.exit.guard118.Flow240_crit_edge, %60
  %1427 = phi i32 [ %62, %60 ], [ poison, %loop.exit.guard118.Flow240_crit_edge ]
  %1428 = phi i32 [ %61, %60 ], [ %.1, %loop.exit.guard118.Flow240_crit_edge ]
  %1429 = phi i1 [ false, %60 ], [ true, %loop.exit.guard118.Flow240_crit_edge ]
  br label %Flow239

Flow241:                                          ; preds = %.Flow241_crit_edge, %Flow242
  %1430 = phi i32 [ %56, %Flow242 ], [ %.035, %.Flow241_crit_edge ]
  %1431 = phi i32 [ %1425, %Flow242 ], [ poison, %.Flow241_crit_edge ]
  %1432 = phi i1 [ %55, %Flow242 ], [ false, %.Flow241_crit_edge ]
  %1433 = phi i1 [ %54, %Flow242 ], [ false, %.Flow241_crit_edge ]
  %1434 = phi i1 [ %53, %Flow242 ], [ false, %.Flow241_crit_edge ]
  %1435 = phi i1 [ false, %Flow242 ], [ true, %.Flow241_crit_edge ]
  %1436 = phi i1 [ %1426, %Flow242 ], [ true, %.Flow241_crit_edge ]
  br i1 %1436, label %loop.exit.guard, label %Flow241._crit_edge

Flow241._crit_edge:                               ; preds = %Flow241
  br label %20

loop.exit.guard116:                               ; preds = %Flow239
  br i1 %52, label %47, label %loop.exit.guard116.Flow242_crit_edge

loop.exit.guard116.Flow242_crit_edge:             ; preds = %loop.exit.guard116
  br label %Flow242

Flow238:                                          ; preds = %loop.exit.guard120.Flow238_crit_edge, %65
  %1437 = phi i32 [ %66, %65 ], [ poison, %loop.exit.guard120.Flow238_crit_edge ]
  %1438 = phi i1 [ false, %65 ], [ true, %loop.exit.guard120.Flow238_crit_edge ]
  br label %Flow237

loop.exit.guard118:                               ; preds = %Flow237
  br i1 %1442, label %60, label %loop.exit.guard118.Flow240_crit_edge

loop.exit.guard118.Flow240_crit_edge:             ; preds = %loop.exit.guard118
  br label %Flow240

Flow236:                                          ; preds = %Flow234.Flow236_crit_edge, %102
  %1439 = phi i32 [ %103, %102 ], [ poison, %Flow234.Flow236_crit_edge ]
  %1440 = phi i1 [ false, %102 ], [ true, %Flow234.Flow236_crit_edge ]
  br label %Flow227

Flow237:                                          ; preds = %.Flow237_crit_edge, %Flow238
  %1441 = phi i32 [ %1437, %Flow238 ], [ poison, %.Flow237_crit_edge ]
  %1442 = phi i1 [ false, %Flow238 ], [ true, %.Flow237_crit_edge ]
  %1443 = phi i1 [ %72, %Flow238 ], [ poison, %.Flow237_crit_edge ]
  %1444 = phi i1 [ %71, %Flow238 ], [ poison, %.Flow237_crit_edge ]
  %1445 = phi i1 [ %70, %Flow238 ], [ poison, %.Flow237_crit_edge ]
  %1446 = phi i1 [ %1438, %Flow238 ], [ true, %.Flow237_crit_edge ]
  br i1 %1446, label %loop.exit.guard118, label %Flow237._crit_edge

Flow237._crit_edge:                               ; preds = %Flow237
  br label %58

loop.exit.guard120:                               ; preds = %Flow227
  br i1 %69, label %65, label %loop.exit.guard120.Flow238_crit_edge

loop.exit.guard120.Flow238_crit_edge:             ; preds = %loop.exit.guard120
  br label %Flow238
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: read)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #3

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { nocallback nofree nosync nounwind willreturn }
attributes #2 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #3 = { nounwind willreturn memory(argmem: read) }

!feme.cpu.resources = !{!5}
!feme.cpu.bound_resources = !{!6}

!0 = !{i32 30, i32 0}
!1 = !{!0}
!2 = !{i32 11, i32 15}
!3 = !{!2}
!4 = !{[1 x i8] c" "}
!5 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!6 = !{!"main", i32 2, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0}
