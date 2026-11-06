; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; A uniform loop whose body diamond ends in the loop's real exit check: one
; successor falls straight through to the latch and the other relays to the
; exit. The pre-check must tolerate it like `flattenLoopBodyRegion` does
; (ENV6(e)(4a), `dEQP-VK.glsl.discard.static_loop_uniform`).

; CHECK-LABEL: define void @main(
; CHECK: call void @feme.cpu.stage.return.masks.i1(

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

declare void @feme.stage.discard(i1)

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
  %.0 = phi i32 [ 0, %0 ], [ %17, %Flow._crit_edge ]
  %10 = icmp slt i32 %.0, 2
  br i1 %10, label %11, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %9
  br label %Flow

common.ret:                                       ; preds = %loop.exit.guard.common.ret_crit_edge, %23
  ret void

11:                                               ; preds = %9
  %12 = icmp eq i32 %.0, 1
  %.inv1 = xor i1 %12, true
  br i1 %12, label %20, label %.Flow2_crit_edge

.Flow2_crit_edge:                                 ; preds = %11
  br label %Flow2

Flow2:                                            ; preds = %.Flow2_crit_edge, %20
  %13 = phi i1 [ false, %20 ], [ true, %.Flow2_crit_edge ]
  %14 = phi i1 [ %22, %20 ], [ %.inv1, %.Flow2_crit_edge ]
  br i1 %14, label %15, label %Flow2.Flow3_crit_edge

Flow2.Flow3_crit_edge:                            ; preds = %Flow2
  br label %Flow3

15:                                               ; preds = %Flow2
  %16 = add i32 %.0, 1
  br label %Flow3

Flow:                                             ; preds = %.Flow_crit_edge, %Flow3
  %17 = phi i32 [ %24, %Flow3 ], [ poison, %.Flow_crit_edge ]
  %18 = phi i1 [ %13, %Flow3 ], [ true, %.Flow_crit_edge ]
  %19 = phi i1 [ %25, %Flow3 ], [ true, %.Flow_crit_edge ]
  br i1 %19, label %loop.exit.guard, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %9

20:                                               ; preds = %11
  %21 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %22 = icmp sle i32 %21, 0
  br label %Flow2

23:                                               ; preds = %loop.exit.guard
  call void @feme.stage.discard(i1 true)
  br label %common.ret

Flow3:                                            ; preds = %Flow2.Flow3_crit_edge, %15
  %24 = phi i32 [ %16, %15 ], [ poison, %Flow2.Flow3_crit_edge ]
  %25 = phi i1 [ false, %15 ], [ true, %Flow2.Flow3_crit_edge ]
  br label %Flow

loop.exit.guard:                                  ; preds = %Flow
  %Guard.common.ret.inv = xor i1 %18, true
  br i1 %Guard.common.ret.inv, label %23, label %loop.exit.guard.common.ret_crit_edge

loop.exit.guard.common.ret_crit_edge:             ; preds = %loop.exit.guard
  br label %common.ret
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #2

attributes #1 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #2 = { nounwind willreturn memory(argmem: read) }
