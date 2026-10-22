; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | opt -passes=verify -S | FileCheck %s

; Roadmap L516: reduced from a real `dEQP-VK.graphicsfuzz.cov-nested-loops-
; assign-vector-elements-from-matrix-no-negative-indexing` shape
; (`FEME_DUMP_IR_PRELINEARIZE=1`, hand-extracted, `instnamer`-named
; values left as-is from the real repro to keep this reduction faithful).
; `loop.exit.guard1` is both the *inner* loop's own `ExitBlock` (reached
; from its masked exit edges `Flow4._crit_edge`/`.Flow5_crit_edge`/
; `.Flow6_crit_edge`, needing a relay-forwarding phi there for the inner
; loop's own loop-carried exit condition) *and* the *outer* loop's own
; `CheckBlock` (its original, single-predecessor `br i1 %55, ...` is the
; outer loop's own recognized divergent exit check). By the time the
; outer loop's own `CheckBuilder` code runs, `CheckExit->Cond` (`%55`'s
; own replacement) is itself one of `loop.exit.guard1`'s own, freshly
; built phis -- so the "insert right after `Cond`'s own instruction" fast
; path (needed so a resource/atomic call between `Cond` and the block's
; terminator picks up this check's own narrower mask, see this same
; code's own L442 comment) landed *between* that phi and
; `freezeLoopCarriedValues`'s own, separately inserted `ShadowExitFixups`
; exit-value phi, splitting two phis apart with freshly built non-phi
; instructions in between -- a "PHI nodes not grouped at top of basic
; block!" verifier failure. Fixed by only taking that fast path when
; `Cond`'s own instruction is not itself a `PHINode`: nothing but another
; phi can validly follow a phi before a block's first non-phi
; instruction, so falling back to `getFirstNonPHIIt()` (already skipping
; every phi, `Cond` and any sibling alike) is exactly equivalent whenever
; `Cond` itself is one, and keeps every phi correctly grouped.

; CHECK-LABEL: loop.exit.guard1:
; CHECK-NEXT: = phi i1
; CHECK-NEXT: = phi i32
; CHECK-NOT: phi
; CHECK: br label

; ModuleID = '<in-memory object>'
source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

@spirv_var_17.str = private constant [13 x i8] c"spirv_var_17\00"
@spirv_var_96.str = private constant [13 x i8] c"spirv_var_96\00"
@spirv_var_104.str = private constant [14 x i8] c"spirv_var_104\00"
@_GLF_color = external addrspace(8) global <4 x float>, !spirv.Decorations !1

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i1 @llvm.vector.reduce.and.v4i1(<4 x i1>) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.spv.discard() #1

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare void @feme.stage.discard(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #2 !feme.signature !2 {
  %1 = alloca [4 x <4 x float>], align 4
  %2 = alloca <4 x float>, align 4
  %3 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %4 = sitofp i32 %3 to float
  %5 = insertelement <4 x float> <float poison, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>, float %4, i64 0
  %6 = insertelement <4 x float> <float 0.000000e+00, float poison, float 0.000000e+00, float 0.000000e+00>, float %4, i64 1
  %7 = insertelement <4 x float> <float 0.000000e+00, float 0.000000e+00, float poison, float 0.000000e+00>, float %4, i64 2
  %8 = insertelement <4 x float> <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float poison>, float %4, i64 3
  store <4 x float> %5, ptr %1, align 4
  %.fca.1.gep = getelementptr inbounds nuw i8, ptr %1, i64 16
  store <4 x float> %6, ptr %.fca.1.gep, align 4
  %.fca.2.gep = getelementptr inbounds nuw i8, ptr %1, i64 32
  store <4 x float> %7, ptr %.fca.2.gep, align 4
  %.fca.3.gep = getelementptr inbounds nuw i8, ptr %1, i64 48
  store <4 x float> %8, ptr %.fca.3.gep, align 4
  %9 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %10 = sitofp i32 %9 to float
  %11 = insertelement <4 x float> <float poison, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>, float %10, i64 0
  %12 = insertelement <4 x float> <float 0.000000e+00, float poison, float 0.000000e+00, float 0.000000e+00>, float %10, i64 1
  %13 = insertelement <4 x float> <float 0.000000e+00, float 0.000000e+00, float poison, float 0.000000e+00>, float %10, i64 2
  %14 = insertelement <4 x float> <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float poison>, float %10, i64 3
  store <4 x float> %11, ptr %1, align 4
  %.fca.1.gep13 = getelementptr inbounds nuw i8, ptr %1, i64 16
  store <4 x float> %12, ptr %.fca.1.gep13, align 4
  %.fca.2.gep15 = getelementptr inbounds nuw i8, ptr %1, i64 32
  store <4 x float> %13, ptr %.fca.2.gep15, align 4
  %.fca.3.gep17 = getelementptr inbounds nuw i8, ptr %1, i64 48
  store <4 x float> %14, ptr %.fca.3.gep17, align 4
  %15 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %16 = sitofp i32 %15 to float
  %17 = insertelement <4 x float> poison, float %16, i64 0
  %18 = shufflevector <4 x float> %17, <4 x float> poison, <4 x i32> zeroinitializer
  store <4 x float> %18, ptr %2, align 4
  %19 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %20 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %21

21:                                               ; preds = %Flow7._crit_edge, %0
  %.019 = phi i32 [ %19, %0 ], [ %45, %Flow7._crit_edge ]
  %.018 = phi i32 [ %20, %0 ], [ %44, %Flow7._crit_edge ]
  %22 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %23 = icmp slt i32 %.019, %22
  br i1 %23, label %42, label %.Flow7_crit_edge

.Flow7_crit_edge:                                 ; preds = %21
  br label %Flow7

Flow2:                                            ; preds = %loop.exit.guard.Flow2_crit_edge, %72
  %24 = phi i1 [ false, %72 ], [ true, %loop.exit.guard.Flow2_crit_edge ]
  br i1 %24, label %25, label %Flow2.Flow3_crit_edge

Flow2.Flow3_crit_edge:                            ; preds = %Flow2
  br label %Flow3

25:                                               ; preds = %Flow2
  %26 = load <4 x float>, ptr %2, align 4
  %27 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %28 = sitofp i32 %27 to float
  %29 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %30 = sitofp i32 %29 to float
  %31 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %32 = sitofp i32 %31 to float
  %33 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 48, i1 true)
  %34 = sitofp i32 %33 to float
  %35 = insertelement <4 x float> poison, float %28, i64 0
  %36 = insertelement <4 x float> %35, float %30, i64 1
  %37 = insertelement <4 x float> %36, float %32, i64 2
  %38 = insertelement <4 x float> %37, float %34, i64 3
  %39 = fcmp une <4 x float> %26, %38
  %40 = bitcast <4 x i1> %39 to i4
  %41 = icmp ne i4 %40, 0
  br i1 %41, label %93, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %25
  br label %Flow

42:                                               ; preds = %21
  %43 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  br label %48

Flow7:                                            ; preds = %.Flow7_crit_edge, %Flow8
  %44 = phi i32 [ %98, %Flow8 ], [ poison, %.Flow7_crit_edge ]
  %45 = phi i32 [ %.1, %Flow8 ], [ poison, %.Flow7_crit_edge ]
  %46 = phi i1 [ false, %Flow8 ], [ true, %.Flow7_crit_edge ]
  %47 = phi i1 [ %99, %Flow8 ], [ true, %.Flow7_crit_edge ]
  br i1 %47, label %loop.exit.guard, label %Flow7._crit_edge

Flow7._crit_edge:                                 ; preds = %Flow7
  br label %21

48:                                               ; preds = %Flow4._crit_edge, %42
  %.1.in = phi i32 [ %.1, %Flow4._crit_edge ], [ %.019, %42 ]
  %.0 = phi i32 [ %54, %Flow4._crit_edge ], [ %43, %42 ]
  %.1 = add i32 %.1.in, 1
  %49 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %.not = icmp sle i32 %.018, %49
  br i1 %.not, label %51, label %.Flow4_crit_edge

.Flow4_crit_edge:                                 ; preds = %48
  br label %Flow4

.critedge:                                        ; preds = %loop.exit.guard1
  %50 = add i32 %.018, 1
  br label %Flow8

51:                                               ; preds = %48
  %52 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %53 = icmp slt i32 %.1, %52
  br i1 %53, label %57, label %.Flow5_crit_edge

.Flow5_crit_edge:                                 ; preds = %51
  br label %Flow5

Flow4:                                            ; preds = %.Flow4_crit_edge, %Flow5
  %54 = phi i32 [ %67, %Flow5 ], [ poison, %.Flow4_crit_edge ]
  %55 = phi i1 [ %68, %Flow5 ], [ true, %.Flow4_crit_edge ]
  %56 = phi i1 [ %69, %Flow5 ], [ true, %.Flow4_crit_edge ]
  br i1 %56, label %loop.exit.guard1, label %Flow4._crit_edge

Flow4._crit_edge:                                 ; preds = %Flow4
  br label %48

57:                                               ; preds = %51
  %58 = sext i32 %.0 to i64
  %59 = sext i32 %.018 to i64
  %.split = getelementptr [16 x i8], ptr %1, i64 %58
  %60 = getelementptr [4 x i8], ptr %.split, i64 %59
  %61 = load float, ptr %60, align 4
  %62 = sext i32 %.0 to i64
  %63 = getelementptr [4 x i8], ptr %2, i64 %62
  store float %61, ptr %63, align 4
  %64 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 0, i1 true)
  %65 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %66 = fcmp ule float %64, %65
  br i1 %66, label %70, label %.Flow6_crit_edge

.Flow6_crit_edge:                                 ; preds = %57
  br label %Flow6

Flow5:                                            ; preds = %.Flow5_crit_edge, %Flow6
  %67 = phi i32 [ %100, %Flow6 ], [ poison, %.Flow5_crit_edge ]
  %68 = phi i1 [ false, %Flow6 ], [ true, %.Flow5_crit_edge ]
  %69 = phi i1 [ %101, %Flow6 ], [ true, %.Flow5_crit_edge ]
  br label %Flow4

70:                                               ; preds = %57
  %71 = add i32 %.0, -1
  br label %Flow6

common.ret:                                       ; preds = %Flow3
  ret void

72:                                               ; preds = %loop.exit.guard
  call void @feme.stage.discard(i1 true)
  br label %Flow2

Flow3:                                            ; preds = %Flow2.Flow3_crit_edge, %73
  br label %common.ret

73:                                               ; preds = %Flow._crit_edge, %80
  %storemerge = phi <4 x float> [ %92, %80 ], [ %78, %Flow._crit_edge ]
  %74 = extractelement <4 x float> %storemerge, i64 0
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %74, i32 0)
  %75 = extractelement <4 x float> %storemerge, i64 1
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %75, i32 0)
  %76 = extractelement <4 x float> %storemerge, i64 2
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %76, i32 0)
  %77 = extractelement <4 x float> %storemerge, i64 3
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %77, i32 0)
  br label %Flow3

Flow:                                             ; preds = %.Flow_crit_edge, %93
  %78 = phi <4 x float> [ %97, %93 ], [ poison, %.Flow_crit_edge ]
  %79 = phi i1 [ false, %93 ], [ true, %.Flow_crit_edge ]
  br i1 %79, label %80, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %73

80:                                               ; preds = %Flow
  %81 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %82 = sitofp i32 %81 to float
  %83 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %84 = sitofp i32 %83 to float
  %85 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %86 = sitofp i32 %85 to float
  %87 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 16, i1 true)
  %88 = sitofp i32 %87 to float
  %89 = insertelement <4 x float> poison, float %82, i64 0
  %90 = insertelement <4 x float> %89, float %84, i64 1
  %91 = insertelement <4 x float> %90, float %86, i64 2
  %92 = insertelement <4 x float> %91, float %88, i64 3
  br label %73

93:                                               ; preds = %25
  %94 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 32, i1 true)
  %95 = sitofp i32 %94 to float
  %96 = insertelement <4 x float> poison, float %95, i64 0
  %97 = shufflevector <4 x float> %96, <4 x float> poison, <4 x i32> zeroinitializer
  br label %Flow

Flow8:                                            ; preds = %loop.exit.guard1.Flow8_crit_edge, %.critedge
  %98 = phi i32 [ %50, %.critedge ], [ poison, %loop.exit.guard1.Flow8_crit_edge ]
  %99 = phi i1 [ false, %.critedge ], [ true, %loop.exit.guard1.Flow8_crit_edge ]
  br label %Flow7

loop.exit.guard:                                  ; preds = %Flow7
  %Guard..inv = xor i1 %46, true
  br i1 %Guard..inv, label %72, label %loop.exit.guard.Flow2_crit_edge

loop.exit.guard.Flow2_crit_edge:                  ; preds = %loop.exit.guard
  br label %Flow2

Flow6:                                            ; preds = %.Flow6_crit_edge, %70
  %100 = phi i32 [ %71, %70 ], [ poison, %.Flow6_crit_edge ]
  %101 = phi i1 [ false, %70 ], [ true, %.Flow6_crit_edge ]
  br label %Flow5

loop.exit.guard1:                                 ; preds = %Flow4
  br i1 %55, label %.critedge, label %loop.exit.guard1.Flow8_crit_edge

loop.exit.guard1.Flow8_crit_edge:                 ; preds = %loop.exit.guard1
  br label %Flow8
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: read)
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #3

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { nocallback nofree nosync nounwind willreturn }
attributes #2 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #3 = { nounwind willreturn memory(argmem: read) }

!feme.cpu.resources = !{!3}
!feme.cpu.bound_resources = !{!4}

!0 = !{i32 30, i32 0}
!1 = !{!0}
!2 = !{[108 x i8] c"\08\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
!3 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!4 = !{!"main", i32 3, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0, i32 0, i32 2, i32 1, i32 2, i32 0}
