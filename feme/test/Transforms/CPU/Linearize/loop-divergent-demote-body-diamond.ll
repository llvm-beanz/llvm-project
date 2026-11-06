; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; A demote guarded by a per-lane condition inside a loop whose exit check
; depends on a resource load. The divergent body diamond is flattened and
; the exit block reports the accumulated masks (ENV6(e)(4b),
; `dEQP-VK.glsl.demote.dynamic_loop_dynamic`).

; CHECK-LABEL: define void @main(
; CHECK: call void @feme.cpu.stage.return.masks.i1(i1 %exit.acc.live.next, i1 %exit.acc.sideeffect.next)

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare void @feme.stage.demote(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #2 {
  %1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %2 = insertelement <4 x float> poison, float %1, i64 0
  %3 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %4 = insertelement <4 x float> %2, float %3, i64 1
  %5 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %6 = insertelement <4 x float> %4, float %5, i64 2
  %7 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %8 = insertelement <4 x float> %6, float %7, i64 3
  call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float %1, i32 0)
  call void @feme.stage.output.store.f32(i32 3, i32 0, i32 1, float %3, i32 0)
  call void @feme.stage.output.store.f32(i32 3, i32 0, i32 2, float %5, i32 0)
  call void @feme.stage.output.store.f32(i32 3, i32 0, i32 3, float %7, i32 0)
  br label %9

9:                                                ; preds = %Flow1._crit_edge, %0
  %.0 = phi i32 [ 0, %0 ], [ %15, %Flow1._crit_edge ]
  %10 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %11 = icmp slt i32 %.0, %10
  br i1 %11, label %13, label %.Flow1_crit_edge

.Flow1_crit_edge:                                 ; preds = %9
  br label %Flow1

12:                                               ; preds = %Flow1
  ret void

13:                                               ; preds = %9
  %14 = icmp sgt i32 %.0, 0
  br i1 %14, label %19, label %._crit_edge

._crit_edge:                                      ; preds = %13
  br label %17

Flow:                                             ; preds = %.Flow_crit_edge, %36
  br label %17

Flow1:                                            ; preds = %.Flow1_crit_edge, %17
  %15 = phi i32 [ %18, %17 ], [ poison, %.Flow1_crit_edge ]
  %16 = phi i1 [ false, %17 ], [ true, %.Flow1_crit_edge ]
  br i1 %16, label %12, label %Flow1._crit_edge

Flow1._crit_edge:                                 ; preds = %Flow1
  br label %9

17:                                               ; preds = %._crit_edge, %Flow
  %18 = add i32 %.0, 1
  br label %Flow1

19:                                               ; preds = %13
  %20 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
  %21 = insertelement <4 x float> poison, float %20, i64 0
  %22 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
  %23 = insertelement <4 x float> %21, float %22, i64 1
  %24 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
  %25 = insertelement <4 x float> %23, float %24, i64 2
  %26 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
  %27 = insertelement <4 x float> %25, float %26, i64 3
  %28 = call i1 @llvm.is.fpclass.f32(float %20, /* (sub) */ i32 144)
  %29 = call float @llvm.copysign.f32(float 0.000000e+00, float %20)
  %30 = select i1 %28, float %29, float %20
  %31 = call i1 @llvm.is.fpclass.f32(float %22, /* (sub) */ i32 144)
  %32 = call float @llvm.copysign.f32(float 0.000000e+00, float %22)
  %33 = select i1 %31, float %32, float %22
  %34 = fadd float %30, %33
  %35 = call i1 @llvm.is.fpclass.f32(float %34, /* (pinf pnorm) */ i32 768)
  br i1 %35, label %36, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %19
  br label %Flow

36:                                               ; preds = %19
  call void @feme.stage.demote(i1 true)
  br label %Flow
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #3

attributes #1 = { nocallback nofree nosync nounwind willreturn }
attributes #2 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #3 = { nounwind willreturn memory(argmem: read) }
