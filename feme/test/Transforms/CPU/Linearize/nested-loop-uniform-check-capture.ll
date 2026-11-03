; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s -o %t
; RUN: feme-opt --llvm -passes=feme-cpu-simdize,verify -feme-cpu-wave-size=4 -S %t -o /dev/null

; Captured from graphicsfuzz.cov-uniform-vector-function-argument-mod-
; increment-integers. Invalidating every user of a new capture without
; checking its actual divergence falsely makes the inner trip-count
; branch divergent and rejects the enclosing loop's pass-through region.

source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

declare float @llvm.floor.f32(float) #0
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
  %push_const.inbounds1 = icmp ule i32 4, %root_constant_size
  br i1 %push_const.inbounds1, label %1, label %2

1:
  %push_const.ptr2 = getelementptr inbounds i8, ptr %root_constants, i64 0
  %push_const.load3 = load float, ptr %push_const.ptr2, align 4
  br label %3

2:
  br label %3

3:
  %push_const.value4 = phi float [ %push_const.load3, %1 ], [ 0.000000e+00, %2 ]
  %push_const.inbounds = icmp ule i32 8, %root_constant_size
  br i1 %push_const.inbounds, label %4, label %5

4:
  %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 4
  %push_const.load = load float, ptr %push_const.ptr, align 4
  br label %6

5:
  br label %6

6:
  %push_const.value = phi float [ %push_const.load, %4 ], [ 0.000000e+00, %5 ]
  %7 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  br label %8

8:
  %.011 = phi i32 [ 0, %6 ], [ %69, %Flow5._crit_edge ]
  %.09 = phi i32 [ 1, %6 ], [ %68, %Flow5._crit_edge ]
  %.0 = phi i32 [ %7, %6 ], [ %67, %Flow5._crit_edge ]
  %9 = icmp slt i32 %.011, 2
  br i1 %9, label %._crit_edge, label %.Flow5_crit_edge

.Flow5_crit_edge:
  br label %Flow5

._crit_edge:
  br label %12

10:
  %11 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  br label %"func(f1;f1;.exit"

12:
  %.110 = phi i32 [ %29, %Flow3._crit_edge ], [ %.09, %._crit_edge ]
  %.1 = phi i32 [ %30, %Flow3._crit_edge ], [ %.0, %._crit_edge ]
  %13 = icmp slt i32 %.110, 110
  br i1 %13, label %16, label %.Flow3_crit_edge

.Flow3_crit_edge:
  br label %Flow3

14:
  %15 = add i32 %.011, 1
  br label %Flow6

16:
  %17 = sitofp i32 %.110 to float
  %18 = fdiv float %17, %push_const.value
  %19 = call float @llvm.floor.f32(float %18)
  %20 = fmul float %push_const.value, %19
  %21 = fsub float %17, %20
  %22 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %23 = fcmp ogt float %21, %22
  %24 = add i32 %.1, 1
  %spec.select = select i1 %23, i32 %24, i32 %.1
  %25 = sitofp i32 %.110 to float
  %26 = fcmp ugt float %push_const.value4, %25
  br i1 %26, label %32, label %.Flow4_crit_edge

.Flow4_crit_edge:
  br label %Flow4

Flow3:
  %27 = phi i1 [ false, %Flow4 ], [ true, %.Flow3_crit_edge ]
  %28 = phi i32 [ %spec.select, %Flow4 ], [ poison, %.Flow3_crit_edge ]
  %29 = phi i32 [ %64, %Flow4 ], [ %.110, %.Flow3_crit_edge ]
  %30 = phi i32 [ %65, %Flow4 ], [ %.1, %.Flow3_crit_edge ]
  %31 = phi i1 [ %66, %Flow4 ], [ true, %.Flow3_crit_edge ]
  br i1 %31, label %loop.exit.guard1, label %Flow3._crit_edge

Flow3._crit_edge:
  br label %12

32:
  %33 = add i32 %.110, 1
  br label %Flow4

"func(f1;f1;.exit":
  %34 = phi i32 [ %71, %"loop.exit.guard.func(f1;f1;.exit_crit_edge" ], [ %11, %10 ]
  %35 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 32, i1 true)
  %36 = icmp ne i32 %34, %35
  br i1 %36, label %57, label %"func(f1;f1;.exit.Flow_crit_edge"

"func(f1;f1;.exit.Flow_crit_edge":
  br label %Flow

37:
  %storemerge = phi <4 x float> [ %42, %Flow._crit_edge ], [ %56, %44 ]
  %38 = extractelement <4 x float> %storemerge, i64 0
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %38, i32 0)
  %39 = extractelement <4 x float> %storemerge, i64 1
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %39, i32 0)
  %40 = extractelement <4 x float> %storemerge, i64 2
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %40, i32 0)
  %41 = extractelement <4 x float> %storemerge, i64 3
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %41, i32 0)
  ret void

Flow:
  %42 = phi <4 x float> [ %61, %57 ], [ poison, %"func(f1;f1;.exit.Flow_crit_edge" ]
  %43 = phi i1 [ false, %57 ], [ true, %"func(f1;f1;.exit.Flow_crit_edge" ]
  br i1 %43, label %44, label %Flow._crit_edge

Flow._crit_edge:
  br label %37

44:
  %45 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %46 = sitofp i32 %45 to float
  %47 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %48 = sitofp i32 %47 to float
  %49 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %50 = sitofp i32 %49 to float
  %51 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %52 = sitofp i32 %51 to float
  %53 = insertelement <4 x float> poison, float %46, i64 0
  %54 = insertelement <4 x float> %53, float %48, i64 1
  %55 = insertelement <4 x float> %54, float %50, i64 2
  %56 = insertelement <4 x float> %55, float %52, i64 3
  br label %37

57:
  %58 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %59 = sitofp i32 %58 to float
  %60 = insertelement <4 x float> poison, float %59, i64 0
  %61 = shufflevector <4 x float> %60, <4 x float> poison, <4 x i32> zeroinitializer
  br label %Flow

Flow6:
  %62 = phi i32 [ %15, %14 ], [ poison, %loop.exit.guard1.Flow6_crit_edge ]
  %63 = phi i1 [ false, %14 ], [ true, %loop.exit.guard1.Flow6_crit_edge ]
  br label %Flow5

loop.exit.guard:
  br i1 %70, label %10, label %"loop.exit.guard.func(f1;f1;.exit_crit_edge"

"loop.exit.guard.func(f1;f1;.exit_crit_edge":
  br label %"func(f1;f1;.exit"

Flow4:
  %64 = phi i32 [ %33, %32 ], [ %.110, %.Flow4_crit_edge ]
  %65 = phi i32 [ %spec.select, %32 ], [ %.1, %.Flow4_crit_edge ]
  %66 = phi i1 [ false, %32 ], [ true, %.Flow4_crit_edge ]
  br label %Flow3

Flow5:
  %67 = phi i32 [ %30, %Flow6 ], [ %.0, %.Flow5_crit_edge ]
  %68 = phi i32 [ %29, %Flow6 ], [ %.09, %.Flow5_crit_edge ]
  %69 = phi i32 [ %62, %Flow6 ], [ poison, %.Flow5_crit_edge ]
  %70 = phi i1 [ false, %Flow6 ], [ true, %.Flow5_crit_edge ]
  %71 = phi i32 [ %28, %Flow6 ], [ poison, %.Flow5_crit_edge ]
  %72 = phi i1 [ %63, %Flow6 ], [ true, %.Flow5_crit_edge ]
  br i1 %72, label %loop.exit.guard, label %Flow5._crit_edge

Flow5._crit_edge:
  br label %8

loop.exit.guard1:
  br i1 %27, label %14, label %loop.exit.guard1.Flow6_crit_edge

loop.exit.guard1.Flow6_crit_edge:
  br label %Flow6
}

declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #2
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #2
attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #2 = { nounwind willreturn memory(argmem: read) }
!feme.cpu.resources = !{!0}
!feme.cpu.bound_resources = !{!1}
!0 = !{!"main", i32 8, i1 false, i32 0, i32 0, i32 0}
!1 = !{!"main", i32 2, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0}
