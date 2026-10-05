; ModuleID = '/Users/cbieneman/dev/llvm-project/feme/test/Transforms/CPU/Linearize/checkblock-chained-divergent-check-bypass.ll'
source_filename = "/Users/cbieneman/dev/llvm-project/feme/test/Transforms/CPU/Linearize/checkblock-chained-divergent-check-bypass.ll"

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
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
  %.0 = phi i32 [ 0, %0 ], [ %.0.frozen, %Flow71._crit_edge ]
  %active.live = phi i1 [ true, %0 ], [ %active.header.live, %Flow71._crit_edge ]
  %active.sideeffect = phi i1 [ true, %0 ], [ %active.header.sideeffect, %Flow71._crit_edge ]
  %22 = call i32 @feme.cpu.resource.getdimensions.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i64 0, i1 true)
  %23 = icmp slt i32 %.0, %22
  %active.header.live = and i1 %active.live, %23
  %active.header.sideeffect = and i1 %active.sideeffect, %23
  br label %28

Flow:                                             ; preds = %loop.exit.guard.Flow_crit_edge
  %live.merge = select i1 %Guard..inv, i1 %live.t, i1 %live.f
  %sideeffect.merge = select i1 %Guard..inv, i1 %sideeffect.t, i1 %sideeffect.f
  %.linearized = select i1 %Guard..inv, i1 false, i1 true
  %not..linearized = xor i1 %.linearized, true
  %live.t2 = and i1 %live.merge, %.linearized
  %sideeffect.t3 = and i1 %sideeffect.merge, %.linearized
  %live.f4 = and i1 %live.merge, %not..linearized
  %sideeffect.f5 = and i1 %sideeffect.merge, %not..linearized
  br label %24

Flow.spirv_fn_12.exit_crit_edge:                  ; preds = %24
  br label %spirv_fn_12.exit

24:                                               ; preds = %Flow
  %25 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 %live.t2)
  br label %Flow.spirv_fn_12.exit_crit_edge

Flow74.Flow75_crit_edge:                          ; preds = %Flow72.Flow74_crit_edge
  br label %loop.exit.guard

26:                                               ; preds = %76, %Flow72.Flow74_crit_edge
  %27 = add i32 %.0, 3
  br label %Flow71._crit_edge

28:                                               ; preds = %21
  %29 = zext i32 %.0 to i64
  %30 = mul i64 %29, 32
  %31 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %30, i1 %active.header.live)
  %32 = shufflevector <4 x float> %31, <4 x float> %31, <2 x i32> <i32 0, i32 1>
  %33 = add i32 %.0, 1
  %34 = zext i32 %33 to i64
  %35 = mul i64 %34, 32
  %36 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %35, i1 %active.header.live)
  %37 = shufflevector <4 x float> %36, <4 x float> %36, <2 x i32> <i32 0, i32 1>
  %38 = add i32 %.0, 2
  %39 = zext i32 %38 to i64
  %40 = mul i64 %39, 32
  %41 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %40, i1 %active.header.live)
  %42 = shufflevector <4 x float> %41, <4 x float> %41, <2 x i32> <i32 0, i32 1>
  %.0.vec.extract37 = extractelement <2 x float> %37, i64 0
  %.0.vec.extract49 = extractelement <2 x float> %32, i64 0
  %43 = fsub float %.0.vec.extract37, %.0.vec.extract49
  %.4.vec.extract29 = extractelement <2 x float> %42, i64 1
  %.4.vec.extract55 = extractelement <2 x float> %32, i64 1
  %44 = fsub float %.4.vec.extract29, %.4.vec.extract55
  %45 = fmul float %43, %44
  %.4.vec.extract43 = extractelement <2 x float> %37, i64 1
  %.4.vec.extract57 = extractelement <2 x float> %32, i64 1
  %46 = fsub float %.4.vec.extract43, %.4.vec.extract57
  %.0.vec.extract21 = extractelement <2 x float> %42, i64 0
  %.0.vec.extract51 = extractelement <2 x float> %32, i64 0
  %47 = fsub float %.0.vec.extract21, %.0.vec.extract51
  %48 = fmul float %46, %47
  %49 = fsub float %45, %48
  %50 = call float @llvm.fabs.f32(float %49)
  %51 = fcmp uge float %50, f0x3727C5AC
  %not. = xor i1 %51, true
  %live.t10 = and i1 true, %51
  %sideeffect.t11 = and i1 true, %51
  %live.f12 = and i1 true, %not.
  %sideeffect.f13 = and i1 true, %not.
  br label %52

.Flow72_crit_edge:                                ; preds = %Flow73
  br label %Flow72

Flow71._crit_edge:                                ; preds = %26
  %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.header.live)
  %.0.frozen = select i1 %active.header.live, i32 %27, i32 %.0
  br i1 %loop.any.active, label %21, label %loop.exit.guard

52:                                               ; preds = %28
  %.0.vec.extract23 = extractelement <2 x float> %42, i64 0
  %.0.vec.extract39 = extractelement <2 x float> %37, i64 0
  %53 = fsub float %.0.vec.extract23, %.0.vec.extract39
  %.4.vec.extract = extractelement <2 x float> %20, i64 1
  %.4.vec.extract45 = extractelement <2 x float> %37, i64 1
  %54 = fsub float %.4.vec.extract, %.4.vec.extract45
  %55 = fmul float %53, %54
  %.4.vec.extract31 = extractelement <2 x float> %42, i64 1
  %.4.vec.extract47 = extractelement <2 x float> %37, i64 1
  %56 = fsub float %.4.vec.extract31, %.4.vec.extract47
  %.0.vec.extract = extractelement <2 x float> %20, i64 0
  %.0.vec.extract41 = extractelement <2 x float> %37, i64 0
  %57 = fsub float %.0.vec.extract, %.0.vec.extract41
  %58 = fmul float %56, %57
  %59 = fsub float %55, %58
  %60 = fdiv float %59, %49
  %.0.vec.extract53 = extractelement <2 x float> %32, i64 0
  %.0.vec.extract25 = extractelement <2 x float> %42, i64 0
  %61 = fsub float %.0.vec.extract53, %.0.vec.extract25
  %.4.vec.extract5 = extractelement <2 x float> %20, i64 1
  %.4.vec.extract33 = extractelement <2 x float> %42, i64 1
  %62 = fsub float %.4.vec.extract5, %.4.vec.extract33
  %63 = fmul float %61, %62
  %.4.vec.extract59 = extractelement <2 x float> %32, i64 1
  %.4.vec.extract35 = extractelement <2 x float> %42, i64 1
  %64 = fsub float %.4.vec.extract59, %.4.vec.extract35
  %.0.vec.extract3 = extractelement <2 x float> %20, i64 0
  %.0.vec.extract27 = extractelement <2 x float> %42, i64 0
  %65 = fsub float %.0.vec.extract3, %.0.vec.extract27
  %66 = fmul float %64, %65
  %67 = fsub float %63, %66
  %68 = fdiv float %67, %49
  %69 = fsub float 1.000000e+00, %60
  %70 = fsub float %69, %68
  %71 = fcmp oge float %60, f0xB727C5AC
  %72 = fcmp oge float %68, f0xB727C5AC
  %73 = and i1 %71, %72
  %74 = fcmp oge float %70, f0xB727C5AC
  %75 = and i1 %73, %74
  %.inv70 = xor i1 %75, true
  %not..inv70 = xor i1 %.inv70, true
  %live.t15 = and i1 %live.t10, %.inv70
  %sideeffect.t16 = and i1 %sideeffect.t11, %.inv70
  %live.f17 = and i1 %live.t10, %not..inv70
  %sideeffect.f18 = and i1 %sideeffect.t11, %not..inv70
  br label %77

.Flow73_crit_edge:                                ; preds = %77
  br label %Flow73

Flow73:                                           ; preds = %.Flow73_crit_edge
  %live.merge19 = select i1 %.inv70, i1 %live.t15, i1 %live.f17
  %sideeffect.merge20 = select i1 %.inv70, i1 %sideeffect.t16, i1 %sideeffect.f18
  %.linearized14 = select i1 %.inv70, i1 true, i1 false
  br label %.Flow72_crit_edge

76:                                               ; preds = %Flow72
  br label %26

Flow72:                                           ; preds = %.Flow72_crit_edge
  %live.merge21 = select i1 %51, i1 %live.merge19, i1 %live.f12
  %sideeffect.merge22 = select i1 %51, i1 %sideeffect.merge20, i1 %sideeffect.f13
  %.linearized8 = select i1 %51, i1 %.linearized14, i1 false
  %.linearized9 = select i1 %51, i1 false, i1 true
  br i1 %.linearized9, label %76, label %Flow72.Flow74_crit_edge

Flow72.Flow74_crit_edge:                          ; preds = %Flow72
  br i1 %.linearized8, label %26, label %Flow74.Flow75_crit_edge

77:                                               ; preds = %52
  br label %.Flow73_crit_edge

78:                                               ; preds = %loop.exit.guard
  %79 = zext i32 %.0 to i64
  %80 = mul i64 %79, 32
  %81 = add i64 %80, 16
  %82 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %81, i1 %live.t)
  %83 = insertelement <4 x float> poison, float %124, i32 0
  %84 = insertelement <4 x float> %83, float %124, i32 1
  %85 = insertelement <4 x float> %84, float %124, i32 2
  %86 = insertelement <4 x float> %85, float %124, i32 3
  %87 = fmul <4 x float> %82, %86
  %88 = add i32 %.0, 1
  %89 = zext i32 %88 to i64
  %90 = mul i64 %89, 32
  %91 = add i64 %90, 16
  %92 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %91, i1 %live.t)
  %93 = insertelement <4 x float> poison, float %123, i32 0
  %94 = insertelement <4 x float> %93, float %123, i32 1
  %95 = insertelement <4 x float> %94, float %123, i32 2
  %96 = insertelement <4 x float> %95, float %123, i32 3
  %97 = fmul <4 x float> %92, %96
  %98 = fadd <4 x float> %87, %97
  %99 = add i32 %.0, 2
  %100 = zext i32 %99 to i64
  %101 = mul i64 %100, 32
  %102 = add i64 %101, 16
  %103 = call <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %102, i1 %live.t)
  %104 = insertelement <4 x float> poison, float %122, i32 0
  %105 = insertelement <4 x float> %104, float %122, i32 1
  %106 = insertelement <4 x float> %105, float %122, i32 2
  %107 = insertelement <4 x float> %106, float %122, i32 3
  %108 = fmul <4 x float> %103, %107
  %109 = fadd <4 x float> %98, %108
  br label %loop.exit.guard.Flow_crit_edge

spirv_fn_12.exit:                                 ; preds = %Flow.spirv_fn_12.exit_crit_edge
  %live.merge6 = select i1 %.linearized, i1 %live.t2, i1 %live.f4
  %sideeffect.merge7 = select i1 %.linearized, i1 %sideeffect.t3, i1 %sideeffect.f5
  %.linearized1 = select i1 %.linearized, <4 x float> %25, <4 x float> %109
  %.0.vec.extract7 = extractelement <4 x float> %.linearized1, i64 0
  %110 = call <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr %image_heap, i32 %image_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, i32 0, i32 0, float %.0.vec.extract7, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i1 true, float 0.000000e+00, i32 0, float -inf, i1 %live.merge6)
  %111 = fmul <4 x float> %110, <float 5.000000e-04, float 1.000000e+00, float 1.000000e+00, float 1.000000e+00>
  %112 = fadd <4 x float> %111, <float 5.000000e-01, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00>
  %113 = call i32 @llvm.spv.thread.id.i32(i32 0)
  %114 = insertelement <3 x i32> poison, i32 %113, i32 0
  %115 = call i32 @llvm.spv.thread.id.i32(i32 1)
  %116 = insertelement <3 x i32> %114, i32 %115, i32 1
  %117 = call i32 @llvm.spv.thread.id.i32(i32 2)
  %118 = insertelement <3 x i32> %116, i32 %117, i32 2
  %119 = shufflevector <3 x i32> %118, <3 x i32> %118, <2 x i32> <i32 0, i32 1>
  %120 = extractelement <2 x i32> %119, i64 0
  %121 = extractelement <2 x i32> %119, i64 1
  call void @feme.cpu.image.store.2d.v4f32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %120, i32 %121, <4 x float> %112, i1 %sideeffect.merge7)
  ret void

loop.exit.guard:                                  ; preds = %Flow71._crit_edge, %Flow74.Flow75_crit_edge
  %122 = phi float [ poison, %Flow71._crit_edge ], [ %70, %Flow74.Flow75_crit_edge ]
  %123 = phi float [ poison, %Flow71._crit_edge ], [ %68, %Flow74.Flow75_crit_edge ]
  %124 = phi float [ poison, %Flow71._crit_edge ], [ %60, %Flow74.Flow75_crit_edge ]
  %125 = phi i1 [ true, %Flow71._crit_edge ], [ %.linearized9, %Flow74.Flow75_crit_edge ]
  %Guard..inv = xor i1 %125, true
  %not.Guard..inv = xor i1 %Guard..inv, true
  %live.t = and i1 true, %Guard..inv
  %sideeffect.t = and i1 true, %Guard..inv
  %live.f = and i1 true, %not.Guard..inv
  %sideeffect.f = and i1 true, %not.Guard..inv
  br label %78

loop.exit.guard.Flow_crit_edge:                   ; preds = %78
  br label %Flow
}

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.i32(i32) #1

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.num.workgroups.i32(i32) #1

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fabs.f32(float) #2

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.getdimensions.raw.i32(ptr, i32, i32, i64, i64, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: read)
declare <4 x float> @feme.cpu.resource.load.raw.v4f32(ptr, i32, i32, i64, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: read)
declare <4 x float> @feme.cpu.image.sample.1d.v4f32(ptr, i32, ptr, i32, i32, i32, float, float, float, float, i1, float, i32, float, i1) #3

; Function Attrs: nounwind willreturn memory(argmem: write)
declare void @feme.cpu.image.store.2d.v4f32(ptr, i32, i32, i32, i32, <4 x float>, i1) #4

; Function Attrs: nounwind willreturn memory(none)
declare i1 @feme.cpu.mask.any(i1) #1

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
attributes #1 = { nounwind willreturn memory(none) }
attributes #2 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #3 = { nounwind willreturn memory(argmem: read) }
attributes #4 = { nounwind willreturn memory(argmem: write) }
