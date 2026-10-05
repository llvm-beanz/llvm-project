; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L377(a): a real, captured pre-`feme-cpu-linearize` IR reduction of
; `dEQP-VK.pipeline.monolithic.image.sampler.view_type.1d.format.*.
; image_size.127x1_compute`'s own barycentric-sampling loop. This loop's
; body contains a second, genuinely divergent branch (`Flow72`, the
; collapsed remnant of a constant-folded `Flow74` diamond -- see `Flow72`'s
; own two phi-to-select-converted operands) immediately upstream of the
; loop's real exit check (`Flow72.Flow74_crit_edge`), in addition to
; `Header`'s (block `21`'s) own uniform trip-count check.
;
; `Flow72`'s *other* successor (`86`) was earlier found, by
; `peelConstantFlowPredecessorsInCycle`, to carry a provably-constant
; value into what was `Flow74`'s phi -- so `86` was redirected to jump
; directly to block `29` (and from there, `Flow75`/`Flow71`), entirely
; bypassing `Flow72.Flow74_crit_edge`'s own check. This used to leave
; `Flow72.Flow74_crit_edge`'s own "stay in loop" successor (ultimately
; block `26`) reachable *both* via `Flow72.Flow74_crit_edge`'s own check
; *and* via this unrelated bypass edge from `86` -- a real, reproduced
; `verifyModule` dominance failure (see `agent_thoughts.md`'s own "L377"
; entry) before roadmap `L377`'s guard started diagnosing it instead.
;
; `Flow72` is not itself a second exit check (neither of its arms reaches
; `ExitBlock` on its own) -- it is an ordinary "if (c1) continue; else if
; (c2) continue; else exit;" shape. Roadmap `L377(a)` recognizes this
; specific, common shape and folds it entirely into a data-flow decision:
; `76` (the bypass block) is pure glue with no instructions of its own,
; so it is deleted outright, and `Flow72`'s own divergent `CondBr` is
; replaced with a plain `select` (forcing "stay" for lanes that used to
; take the bypass) feeding `Flow72.Flow74_crit_edge`'s own check, plus a
; single, uniform, unconditional branch straight into it -- so the loop
; both linearizes *and* has no divergent branch left over for
; `feme-cpu-simdize` to reject afterward.

; The two operands folded into `%chain.merge` are themselves earlier
; `DiamondFlattener::flatten`-generated `.linearized<N>` values (each
; auto-named by appending a whole-function-wide uniquing counter to a
; shared, unnamed-`phi`-derived base name) -- their exact numeric suffixes
; depend on how many other such values this function's *other* diamonds
; happen to produce first, which can differ across LLVM/platform builds
; without the underlying IR shape changing at all. Match the structural
; pattern instead of hardcoding those suffixes so this test does not
; depend on numbering that was never meant to be stable.
; CHECK: %chain.merge = select i1 {{%[A-Za-z0-9_.]+}}, i1 true, i1 {{%[A-Za-z0-9_.]+}}
; CHECK-NEXT: br label %Flow72.Flow74_crit_edge
; CHECK-NOT: label %76

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #4 {
  %1 = call i32 @llvm.spv.thread.id.i32(i32 0)
  %2 = insertelement <3 x i32> poison, i32 %1, i32 0
  %3 = call i32 @llvm.spv.thread.id.i32(i32 1)
  %4 = insertelement <3 x i32> %2, i32 %3, i32 1
  %5 = call i32 @llvm.spv.thread.id.i32(i32 2)
  %6 = insertelement <3 x i32> %4, i32 %5, i32 2
  %7 = shufflevector <3 x i32> %6, <3 x i32> %6, <2 x i32> <i32 0, i32 1>
  %8 = uitofp <2 x i32> %7 to <2 x float>
  %9 = fadd <2 x float> %8, splat (float 5.000000e-01)
  %10 = call i32 @llvm.spv.num.workgroups.i32(i32 0)
  %11 = insertelement <3 x i32> poison, i32 %10, i32 0
  %12 = call i32 @llvm.spv.num.workgroups.i32(i32 1)
  %13 = insertelement <3 x i32> %11, i32 %12, i32 1
  %14 = call i32 @llvm.spv.num.workgroups.i32(i32 2)
  %15 = insertelement <3 x i32> %13, i32 %14, i32 2
  %16 = shufflevector <3 x i32> %15, <3 x i32> %15, <2 x i32> <i32 0, i32 1>
  %17 = uitofp <2 x i32> %16 to <2 x float>
  %18 = fdiv <2 x float> %9, %17
  %19 = fmul <2 x float> %18, splat (float 2.000000e+00)
  %20 = fsub <2 x float> %19, splat (float 1.000000e+00)
  br label %21

21:                                               ; preds = %Flow71._crit_edge, %0
  %.0 = phi i32 [ 0, %0 ], [ %56, %Flow71._crit_edge ]
  %22 = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i64 0, i1 true)
  %23 = icmp slt i32 %.0, %22
  br i1 %23, label %31, label %.Flow71_crit_edge

.Flow71_crit_edge:                                ; preds = %21
  br label %Flow71

Flow:                                             ; preds = %loop.exit.guard.Flow_crit_edge, %93
  %24 = phi <4 x float> [ %124, %93 ], [ poison, %loop.exit.guard.Flow_crit_edge ]
  %25 = phi i1 [ false, %93 ], [ true, %loop.exit.guard.Flow_crit_edge ]
  br i1 %25, label %26, label %Flow.spirv_fn_12.exit_crit_edge

Flow.spirv_fn_12.exit_crit_edge:                  ; preds = %Flow
  br label %spirv_fn_12.exit

26:                                               ; preds = %Flow
  %27 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  br label %spirv_fn_12.exit

Flow74:                                           ; preds = %Flow72.Flow74_crit_edge, %86
  %28 = phi i1 [ true, %86 ], [ %90, %Flow72.Flow74_crit_edge ]
  br i1 %28, label %29, label %Flow74.Flow75_crit_edge

Flow74.Flow75_crit_edge:                          ; preds = %Flow74
  br label %Flow75

29:                                               ; preds = %Flow74
  %30 = add i32 %.0, 3
  br label %Flow75

31:                                               ; preds = %21
  %32 = zext i32 %.0 to i64
  %33 = mul i64 %32, 32
  %34 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %33, i1 true)
  %35 = shufflevector <4 x float> %34, <4 x float> %34, <2 x i32> <i32 0, i32 1>
  %36 = add i32 %.0, 1
  %37 = zext i32 %36 to i64
  %38 = mul i64 %37, 32
  %39 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %38, i1 true)
  %40 = shufflevector <4 x float> %39, <4 x float> %39, <2 x i32> <i32 0, i32 1>
  %41 = add i32 %.0, 2
  %42 = zext i32 %41 to i64
  %43 = mul i64 %42, 32
  %44 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %43, i1 true)
  %45 = shufflevector <4 x float> %44, <4 x float> %44, <2 x i32> <i32 0, i32 1>
  %.0.vec.extract37 = extractelement <2 x float> %40, i64 0
  %.0.vec.extract49 = extractelement <2 x float> %35, i64 0
  %46 = fsub float %.0.vec.extract37, %.0.vec.extract49
  %.4.vec.extract29 = extractelement <2 x float> %45, i64 1
  %.4.vec.extract55 = extractelement <2 x float> %35, i64 1
  %47 = fsub float %.4.vec.extract29, %.4.vec.extract55
  %48 = fmul float %46, %47
  %.4.vec.extract43 = extractelement <2 x float> %40, i64 1
  %.4.vec.extract57 = extractelement <2 x float> %35, i64 1
  %49 = fsub float %.4.vec.extract43, %.4.vec.extract57
  %.0.vec.extract21 = extractelement <2 x float> %45, i64 0
  %.0.vec.extract51 = extractelement <2 x float> %35, i64 0
  %50 = fsub float %.0.vec.extract21, %.0.vec.extract51
  %51 = fmul float %49, %50
  %52 = fsub float %48, %51
  %53 = call float @llvm.fabs.f32(float %52)
  %54 = fcmp uge float %53, f0x3727C5AC
  br i1 %54, label %61, label %.Flow72_crit_edge

.Flow72_crit_edge:                                ; preds = %31
  br label %Flow72

Flow71:                                           ; preds = %.Flow71_crit_edge, %Flow75
  %55 = phi i1 [ %91, %Flow75 ], [ true, %.Flow71_crit_edge ]
  %56 = phi i32 [ %138, %Flow75 ], [ %.0, %.Flow71_crit_edge ]
  %57 = phi float [ %87, %Flow75 ], [ poison, %.Flow71_crit_edge ]
  %58 = phi float [ %88, %Flow75 ], [ poison, %.Flow71_crit_edge ]
  %59 = phi float [ %89, %Flow75 ], [ poison, %.Flow71_crit_edge ]
  %60 = phi i1 [ %139, %Flow75 ], [ true, %.Flow71_crit_edge ]
  br i1 %60, label %loop.exit.guard, label %Flow71._crit_edge

Flow71._crit_edge:                                ; preds = %Flow71
  br label %21

61:                                               ; preds = %31
  %.0.vec.extract23 = extractelement <2 x float> %45, i64 0
  %.0.vec.extract39 = extractelement <2 x float> %40, i64 0
  %62 = fsub float %.0.vec.extract23, %.0.vec.extract39
  %.4.vec.extract = extractelement <2 x float> %20, i64 1
  %.4.vec.extract45 = extractelement <2 x float> %40, i64 1
  %63 = fsub float %.4.vec.extract, %.4.vec.extract45
  %64 = fmul float %62, %63
  %.4.vec.extract31 = extractelement <2 x float> %45, i64 1
  %.4.vec.extract47 = extractelement <2 x float> %40, i64 1
  %65 = fsub float %.4.vec.extract31, %.4.vec.extract47
  %.0.vec.extract = extractelement <2 x float> %20, i64 0
  %.0.vec.extract41 = extractelement <2 x float> %40, i64 0
  %66 = fsub float %.0.vec.extract, %.0.vec.extract41
  %67 = fmul float %65, %66
  %68 = fsub float %64, %67
  %69 = fdiv float %68, %52
  %.0.vec.extract53 = extractelement <2 x float> %35, i64 0
  %.0.vec.extract25 = extractelement <2 x float> %45, i64 0
  %70 = fsub float %.0.vec.extract53, %.0.vec.extract25
  %.4.vec.extract5 = extractelement <2 x float> %20, i64 1
  %.4.vec.extract33 = extractelement <2 x float> %45, i64 1
  %71 = fsub float %.4.vec.extract5, %.4.vec.extract33
  %72 = fmul float %70, %71
  %.4.vec.extract59 = extractelement <2 x float> %35, i64 1
  %.4.vec.extract35 = extractelement <2 x float> %45, i64 1
  %73 = fsub float %.4.vec.extract59, %.4.vec.extract35
  %.0.vec.extract3 = extractelement <2 x float> %20, i64 0
  %.0.vec.extract27 = extractelement <2 x float> %45, i64 0
  %74 = fsub float %.0.vec.extract3, %.0.vec.extract27
  %75 = fmul float %73, %74
  %76 = fsub float %72, %75
  %77 = fdiv float %76, %52
  %78 = fsub float 1.000000e+00, %69
  %79 = fsub float %78, %77
  %80 = fcmp oge float %69, f0xB727C5AC
  %81 = fcmp oge float %77, f0xB727C5AC
  %82 = and i1 %80, %81
  %83 = fcmp oge float %79, f0xB727C5AC
  %84 = and i1 %82, %83
  %.inv70 = xor i1 %84, true
  br i1 %.inv70, label %92, label %.Flow73_crit_edge

.Flow73_crit_edge:                                ; preds = %61
  br label %Flow73

Flow73:                                           ; preds = %.Flow73_crit_edge, %92
  %85 = phi i1 [ true, %92 ], [ false, %.Flow73_crit_edge ]
  br label %Flow72

86:                                               ; preds = %Flow72
  br label %Flow74

Flow72:                                           ; preds = %.Flow72_crit_edge, %Flow73
  %87 = phi float [ %69, %Flow73 ], [ poison, %.Flow72_crit_edge ]
  %88 = phi float [ %77, %Flow73 ], [ poison, %.Flow72_crit_edge ]
  %89 = phi float [ %79, %Flow73 ], [ poison, %.Flow72_crit_edge ]
  %90 = phi i1 [ %85, %Flow73 ], [ false, %.Flow72_crit_edge ]
  %91 = phi i1 [ false, %Flow73 ], [ true, %.Flow72_crit_edge ]
  br i1 %91, label %86, label %Flow72.Flow74_crit_edge

Flow72.Flow74_crit_edge:                          ; preds = %Flow72
  br label %Flow74

92:                                               ; preds = %61
  br label %Flow73

93:                                               ; preds = %loop.exit.guard
  %94 = zext i32 %56 to i64
  %95 = mul i64 %94, 32
  %96 = add i64 %95, 16
  %97 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %96, i1 true)
  %98 = insertelement <4 x float> poison, float %57, i32 0
  %99 = insertelement <4 x float> %98, float %57, i32 1
  %100 = insertelement <4 x float> %99, float %57, i32 2
  %101 = insertelement <4 x float> %100, float %57, i32 3
  %102 = fmul <4 x float> %97, %101
  %103 = add i32 %56, 1
  %104 = zext i32 %103 to i64
  %105 = mul i64 %104, 32
  %106 = add i64 %105, 16
  %107 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %106, i1 true)
  %108 = insertelement <4 x float> poison, float %58, i32 0
  %109 = insertelement <4 x float> %108, float %58, i32 1
  %110 = insertelement <4 x float> %109, float %58, i32 2
  %111 = insertelement <4 x float> %110, float %58, i32 3
  %112 = fmul <4 x float> %107, %111
  %113 = fadd <4 x float> %102, %112
  %114 = add i32 %56, 2
  %115 = zext i32 %114 to i64
  %116 = mul i64 %115, 32
  %117 = add i64 %116, 16
  %118 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %117, i1 true)
  %119 = insertelement <4 x float> poison, float %59, i32 0
  %120 = insertelement <4 x float> %119, float %59, i32 1
  %121 = insertelement <4 x float> %120, float %59, i32 2
  %122 = insertelement <4 x float> %121, float %59, i32 3
  %123 = fmul <4 x float> %118, %122
  %124 = fadd <4 x float> %113, %123
  br label %Flow

spirv_fn_12.exit:                                 ; preds = %Flow.spirv_fn_12.exit_crit_edge, %26
  %125 = phi <4 x float> [ %24, %Flow.spirv_fn_12.exit_crit_edge ], [ %27, %26 ]
  %.0.vec.extract7 = extractelement <4 x float> %125, i64 0
  %126 = call <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %.0.vec.extract7, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float 0.000000e+00, i32 0, float -inf, i1 true)
  %127 = fmul <4 x float> %126, <float 5.000000e-04, float 1.000000e+00, float 1.000000e+00, float 1.000000e+00>
  %128 = fadd <4 x float> %127, <float 5.000000e-01, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>
  %129 = call i32 @llvm.spv.thread.id.i32(i32 0)
  %130 = insertelement <3 x i32> poison, i32 %129, i32 0
  %131 = call i32 @llvm.spv.thread.id.i32(i32 1)
  %132 = insertelement <3 x i32> %130, i32 %131, i32 1
  %133 = call i32 @llvm.spv.thread.id.i32(i32 2)
  %134 = insertelement <3 x i32> %132, i32 %133, i32 2
  %135 = shufflevector <3 x i32> %134, <3 x i32> %134, <2 x i32> <i32 0, i32 1>
  %136 = extractelement <2 x i32> %135, i64 0
  %137 = extractelement <2 x i32> %135, i64 1
  call void @feme.cpu.image.store.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %136, i32 %137, <4 x float> %128, i1 true)
  ret void

Flow75:                                           ; preds = %Flow74.Flow75_crit_edge, %29
  %138 = phi i32 [ %30, %29 ], [ %.0, %Flow74.Flow75_crit_edge ]
  %139 = phi i1 [ false, %29 ], [ true, %Flow74.Flow75_crit_edge ]
  br label %Flow71

loop.exit.guard:                                  ; preds = %Flow71
  %Guard..inv = xor i1 %55, true
  br i1 %Guard..inv, label %93, label %loop.exit.guard.Flow_crit_edge

loop.exit.guard.Flow_crit_edge:                   ; preds = %loop.exit.guard
  br label %Flow
}

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.i32(i32) #0

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.num.workgroups.i32(i32) #0

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fabs.f32(float) #2

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.getdimensions.raw.i32(ptr, i32, i32, i64, i64, i1) #5

; Function Attrs: nounwind willreturn memory(argmem: read)
declare <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr, i32, i32, i64, i1) #5

; Function Attrs: nounwind willreturn memory(argmem: read)
declare <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr, i32, ptr, i32, i32, i32, float, float, float, float, i1, float, i32, float, i1) #5

; Function Attrs: nounwind willreturn memory(argmem: write)
declare void @feme.cpu.image.store.2d.v4f32(ptr, i32, i32, i32, i32, <4 x float>, i1) #6

attributes #0 = { nounwind willreturn memory(none) }
attributes #2 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #4 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
attributes #5 = { nounwind willreturn memory(argmem: read) }
attributes #6 = { nounwind willreturn memory(argmem: write) }
