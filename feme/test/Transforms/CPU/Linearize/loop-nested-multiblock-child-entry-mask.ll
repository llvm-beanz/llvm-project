; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; A multi-block inner do-while nested in an outer loop with a divergent trip
; count. The outer walk reaches the inner loop at its latch, so the inner
; entry mask must still be narrowed by the outer mask; otherwise lanes the
; outer loop already deactivated enter the inner loop "active" and its
; `feme.cpu.mask.any` never goes false (ENV6(c)(4a), `dowhile_trap` hang).

; CHECK-LABEL: define void @main(
; CHECK: %active.live = phi i1 [ %active.header.live{{[0-9]*}}, %._crit_edge ], [ %active.header.live, %._crit_edge1 ]

declare <4 x i1> @llvm.is.fpclass.v4f32(<4 x float>, i32 immarg)
declare <4 x float> @llvm.copysign.v4f32(<4 x float>, <4 x float>)
declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
  %1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %2 = insertelement <4 x float> poison, float %1, i64 0
  %3 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %4 = insertelement <4 x float> %2, float %3, i64 1
  %5 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %6 = insertelement <4 x float> %4, float %5, i64 2
  %7 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %8 = insertelement <4 x float> %6, float %7, i64 3
  %9 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
  %10 = insertelement <4 x float> poison, float %9, i64 0
  %11 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
  %12 = insertelement <4 x float> %10, float %11, i64 1
  %13 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
  %14 = insertelement <4 x float> %12, float %13, i64 2
  %15 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
  %16 = insertelement <4 x float> %14, float %15, i64 3
  br label %17

17:                                               ; preds = %Flow._crit_edge, %0
  %.07 = phi <4 x float> [ %16, %0 ], [ %43, %Flow._crit_edge ]
  %.0 = phi i32 [ 0, %0 ], [ %42, %Flow._crit_edge ]
  %18 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %19 = icmp slt i32 %.0, %18
  br i1 %19, label %._crit_edge, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %17
  br label %Flow

._crit_edge:                                      ; preds = %17
  br label %32

20:                                               ; preds = %Flow
  %21 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %.07, i32 144)
  %22 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %.07)
  %23 = select <4 x i1> %21, <4 x float> %22, <4 x float> %.07
  %24 = fadd <4 x float> %23, splat (float -3.000000e+00)
  %25 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %24, i32 144)
  %26 = call <4 x float> @llvm.copysign.v4f32(<4 x float> <float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float poison>, <4 x float> %24)
  %27 = select <4 x i1> %25, <4 x float> %26, <4 x float> %24
  %28 = shufflevector <4 x float> %27, <4 x float> poison, <3 x i32> <i32 0, i32 1, i32 2>
  %29 = extractelement <3 x float> %28, i64 0
  call void @feme.stage.output.store.f32(i32 4, i32 0, i32 0, float %29, i32 0)
  %30 = extractelement <3 x float> %28, i64 1
  call void @feme.stage.output.store.f32(i32 4, i32 0, i32 1, float %30, i32 0)
  %31 = extractelement <3 x float> %28, i64 2
  call void @feme.stage.output.store.f32(i32 4, i32 0, i32 2, float %31, i32 0)
  ret void

32:                                               ; preds = %._crit_edge1, %._crit_edge
  %.1 = phi <4 x float> [ %40, %._crit_edge1 ], [ %.07, %._crit_edge ]
  %33 = shufflevector <4 x float> %.1, <4 x float> poison, <4 x i32> <i32 1, i32 2, i32 3, i32 0>
  %34 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %33, i32 144)
  %35 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %33)
  %36 = select <4 x i1> %34, <4 x float> %35, <4 x float> %33
  %37 = fadd <4 x float> %36, splat (float 1.000000e+00)
  %38 = call <4 x i1> @llvm.is.fpclass.v4f32(<4 x float> %37, i32 144)
  %39 = call <4 x float> @llvm.copysign.v4f32(<4 x float> zeroinitializer, <4 x float> %37)
  %40 = select <4 x i1> %38, <4 x float> %39, <4 x float> %37
  %41 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %.not = icmp slt i32 %.0, %41
  br i1 %.not, label %45, label %._crit_edge1

._crit_edge1:                                     ; preds = %32
  br label %32

Flow:                                             ; preds = %.Flow_crit_edge, %45
  %42 = phi i32 [ %46, %45 ], [ poison, %.Flow_crit_edge ]
  %43 = phi <4 x float> [ %40, %45 ], [ %.07, %.Flow_crit_edge ]
  %44 = phi i1 [ false, %45 ], [ true, %.Flow_crit_edge ]
  br i1 %44, label %20, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %17

45:                                               ; preds = %32
  %46 = add i32 %.0, 1
  br label %Flow
}

attributes #1 = { "feme.cpu.wavesize"="4" }
