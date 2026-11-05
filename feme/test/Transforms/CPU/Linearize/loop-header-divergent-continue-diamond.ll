; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; A do-while loop whose header holds a divergent `if (...) continue;` diamond
; (both arms stay in the loop) and whose latch is the only exit. The header
; branch is split into the body so it is handled as an internal diamond
; instead of being diagnosed as a second divergent exit check
; (ENV6(c)(4)).

; CHECK-LABEL: define void @main(
; CHECK: .body:
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(
; CHECK-NOT: unsupported

declare i1 @llvm.is.fpclass.f32(float, i32 immarg)
declare float @llvm.copysign.f32(float, float)
declare <4 x i1> @llvm.is.fpclass.v4f32(<4 x float>, i32 immarg)
declare <4 x float> @llvm.copysign.v4f32(<4 x float>, <4 x float>)
declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
  %1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %2 = call i1 @llvm.is.fpclass.f32(float %1, i32 144)
  %3 = call float @llvm.copysign.f32(float 0.000000e+00, float %1)
  %4 = select i1 %2, float %3, float %1
  %5 = fadd float %4, 5.000000e-01
  %6 = call i1 @llvm.is.fpclass.f32(float %5, i32 144)
  %7 = call float @llvm.copysign.f32(float 0.000000e+00, float %5)
  %8 = select i1 %6, float %7, float %5
  %9 = fptosi float %8 to i32
  %10 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
  %11 = insertelement <4 x float> poison, float %10, i64 0
  %12 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
  %13 = insertelement <4 x float> %11, float %12, i64 1
  %14 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
  %15 = insertelement <4 x float> %13, float %14, i64 2
  %16 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
  %17 = insertelement <4 x float> %15, float %16, i64 3
  br label %18

18:                                               ; preds = %._crit_edge1, %0
  %.08 = phi <4 x float> [ %17, %0 ], [ %.1, %._crit_edge1 ]
  %.0 = phi i32 [ 0, %0 ], [ %35, %._crit_edge1 ]
  %19 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %20 = mul i32 %19, %9
  %21 = icmp ne i32 %.0, %20
  br i1 %21, label %39, label %._crit_edge

._crit_edge:                                      ; preds = %18
  br label %34

22:                                               ; preds = %34
  %23 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %.1, i32 144)
  %24 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %.1)
  %25 = select <4 x i1> %23, <4 x float> %24, <4 x float> %.1
  %26 = fadd <4 x float> %25, splat (float -2.000000e+00)
  %27 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %26, i32 144)
  %28 = call <4 x float> @llvm.copysign.v4f32(<4 x float> <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float poison>, <4 x float> %26)
  %29 = select <4 x i1> %27, <4 x float> %28, <4 x float> %26
  %30 = insertelement <4 x float> %29, float 1.000000e+00, i64 3
  %31 = extractelement <4 x float> %30, i64 0
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %31, i32 0)
  %32 = extractelement <4 x float> %30, i64 1
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %32, i32 0)
  %33 = extractelement <4 x float> %30, i64 2
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %33, i32 0)
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.000000e+00, i32 0)
  ret void

34:                                               ; preds = %._crit_edge, %39
  %.1 = phi <4 x float> [ %47, %39 ], [ %.08, %._crit_edge ]
  %35 = add i32 %.0, 1
  %36 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %37 = mul i32 %36, %9
  %38 = icmp sge i32 %35, %37
  br i1 %38, label %22, label %._crit_edge1

._crit_edge1:                                     ; preds = %34
  br label %18

39:                                               ; preds = %18
  %40 = shufflevector <4 x float> %.08, <4 x float> poison, <4 x i32> <i32 1, i32 2, i32 3, i32 0>
  %41 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %40, i32 144)
  %42 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %40)
  %43 = select <4 x i1> %41, <4 x float> %42, <4 x float> %40
  %44 = fadd <4 x float> %43, splat (float 1.000000e+00)
  %45 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %44, i32 144)
  %46 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %44)
  %47 = select <4 x i1> %45, <4 x float> %46, <4 x float> %44
  br label %34
}

attributes #1 = { "feme.cpu.wavesize"="4" }
