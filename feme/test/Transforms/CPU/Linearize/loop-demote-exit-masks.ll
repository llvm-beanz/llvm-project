; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; A `demote` inside a loop whose exit check depends on a resource load. The
; loop's exit block must report the per-lane accumulated live/side-effect
; masks, not the pre-loop `true, true` (ENV6(e)(4),
; `dEQP-VK.glsl.demote.dynamic_loop_always`).

; CHECK-LABEL: define void @main(
; CHECK: %exit.acc.sideeffect = phi i1 [ %exit.acc.sideeffect.next, %{{.*}} ], [ true, %0 ]
; CHECK-DAG: %exit.acc.sideeffect.next = select i1 %active.header.live, i1 %sideeffect.merge, i1 %exit.acc.sideeffect
; CHECK-DAG: call void @feme.cpu.stage.return.masks.i1(i1 %exit.acc.live.next, i1 %exit.acc.sideeffect.next)

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare void @feme.stage.demote(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 {
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

9:                                                ; preds = %Flow._crit_edge, %0
  %.0 = phi i32 [ 0, %0 ], [ %15, %Flow._crit_edge ]
  %10 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %11 = icmp slt i32 %.0, %10
  br i1 %11, label %13, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %9
  br label %Flow

12:                                               ; preds = %Flow
  ret void

13:                                               ; preds = %9
  %14 = icmp sgt i32 %.0, 0
  br i1 %14, label %19, label %._crit_edge

._crit_edge:                                      ; preds = %13
  br label %17

Flow:                                             ; preds = %.Flow_crit_edge, %17
  %15 = phi i32 [ %18, %17 ], [ poison, %.Flow_crit_edge ]
  %16 = phi i1 [ false, %17 ], [ true, %.Flow_crit_edge ]
  br i1 %16, label %12, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %9

17:                                               ; preds = %._crit_edge, %19
  %18 = add i32 %.0, 1
  br label %Flow

19:                                               ; preds = %13
  call void @feme.stage.demote(i1 true)
  br label %17
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #2

attributes #1 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #2 = { nounwind willreturn memory(argmem: read) }

