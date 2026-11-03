; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s -o %t
; RUN: feme-opt --llvm -passes=feme-cpu-simdize,verify -feme-cpu-wave-size=4 -S %t -o /dev/null

; Captured from graphicsfuzz.complex-nested-loops-and-call. Freezing the
; middle loop's constant false check-exit tag introduces a poison shadow.
; This makes loop.exit.guard1 divergent and breaks the outer loop's
; uniform pass-through traversal. The varying vector capture still needs
; a sticky shadow; both linearization and SIMD widening must succeed.

source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare void @feme.stage.discard(i1)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
  %1 = alloca [16 x <3 x float>], align 4
  br label %2

2:
  %3 = phi i32 [ 0, %0 ], [ %55, %Flow12._crit_edge ]
  %4 = phi <3 x float> [ undef, %0 ], [ %54, %Flow12._crit_edge ]
  %5 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 4, i1 true)
  %6 = fptosi float %5 to i32
  %7 = icmp slt i32 %3, %6
  br i1 %7, label %._crit_edge, label %.Flow12_crit_edge

.Flow12_crit_edge:
  br label %Flow12

._crit_edge:
  br label %16

common.ret:
  ret void

Flow:
  %8 = phi i1 [ false, %48 ], [ true, %loop.exit.guard.Flow_crit_edge ]
  br i1 %8, label %9, label %Flow.common.ret_crit_edge

Flow.common.ret_crit_edge:
  br label %common.ret

9:
  %10 = load <3 x float>, ptr %1, align 4
  %11 = shufflevector <3 x float> %10, <3 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 poison>
  %12 = insertelement <4 x float> %11, float 1.000000e+00, i64 3
  %13 = extractelement <4 x float> %12, i64 0
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %13, i32 0)
  %14 = extractelement <4 x float> %12, i64 1
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %14, i32 0)
  %15 = extractelement <4 x float> %12, i64 2
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %15, i32 0)
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float 1.000000e+00, i32 0)
  br label %common.ret

16:
  %17 = phi <3 x float> [ %27, %Flow10._crit_edge ], [ %4, %._crit_edge ]
  %18 = phi i32 [ %25, %Flow10._crit_edge ], [ 0, %._crit_edge ]
  %19 = icmp slt i32 %18, 2
  br i1 %19, label %22, label %.Flow10_crit_edge

.Flow10_crit_edge:
  br label %Flow10

20:
  %21 = add i32 %3, 1
  br label %Flow13

22:
  %23 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %24 = fptosi float %23 to i32
  br label %29

Flow10:
  %25 = phi i32 [ %51, %Flow11 ], [ poison, %.Flow10_crit_edge ]
  %26 = phi i1 [ false, %Flow11 ], [ true, %.Flow10_crit_edge ]
  %27 = phi <3 x float> [ %52, %Flow11 ], [ %17, %.Flow10_crit_edge ]
  %28 = phi i1 [ %53, %Flow11 ], [ true, %.Flow10_crit_edge ]
  br i1 %28, label %loop.exit.guard1, label %Flow10._crit_edge

Flow10._crit_edge:
  br label %16

29:
  %30 = phi i32 [ %43, %Flow8._crit_edge ], [ 1, %22 ]
  %31 = fcmp uge float %23, 1.000000e+01
  br i1 %31, label %39, label %.Flow8_crit_edge

.Flow8_crit_edge:
  br label %Flow8

32:
  %33 = sitofp i32 %43 to float
  %34 = insertelement <3 x float> <float poison, float 0.000000e+00, float 0.000000e+00>, float %33, i64 0
  %35 = select i1 %31, <3 x float> %42, <3 x float> %34
  %36 = sext i32 %24 to i64
  %37 = getelementptr [16 x i8], ptr %1, i64 %36
  store <3 x float> %35, ptr %37, align 4
  %38 = add i32 %18, 1
  br label %Flow11

39:
  %40 = fcmp uge float %23, 2.000000e+01
  br i1 %40, label %45, label %.Flow9_crit_edge

.Flow9_crit_edge:
  br label %Flow9

Flow8:
  %41 = phi i1 [ %58, %Flow9 ], [ true, %.Flow8_crit_edge ]
  %42 = phi <3 x float> [ splat (float 1.000000e+00), %Flow9 ], [ %17, %.Flow8_crit_edge ]
  %43 = phi i32 [ %59, %Flow9 ], [ %30, %.Flow8_crit_edge ]
  %44 = phi i1 [ %60, %Flow9 ], [ true, %.Flow8_crit_edge ]
  br i1 %44, label %loop.exit.guard3, label %Flow8._crit_edge

Flow8._crit_edge:
  br label %29

45:
  %46 = add i32 %30, 1
  %47 = fcmp olt float %23, 3.000000e+01
  br label %Flow9

48:
  call void @feme.stage.discard(i1 true)
  br label %Flow

Flow13:
  %49 = phi i32 [ %21, %20 ], [ poison, %loop.exit.guard1.Flow13_crit_edge ]
  %50 = phi i1 [ false, %20 ], [ true, %loop.exit.guard1.Flow13_crit_edge ]
  br label %Flow12

loop.exit.guard:
  %Guard..inv = xor i1 %56, true
  br i1 %Guard..inv, label %48, label %loop.exit.guard.Flow_crit_edge

loop.exit.guard.Flow_crit_edge:
  br label %Flow

Flow11:
  %51 = phi i32 [ %38, %32 ], [ poison, %loop.exit.guard3.Flow11_crit_edge ]
  %52 = phi <3 x float> [ %35, %32 ], [ %17, %loop.exit.guard3.Flow11_crit_edge ]
  %53 = phi i1 [ false, %32 ], [ true, %loop.exit.guard3.Flow11_crit_edge ]
  br label %Flow10

Flow12:
  %54 = phi <3 x float> [ %27, %Flow13 ], [ %4, %.Flow12_crit_edge ]
  %55 = phi i32 [ %49, %Flow13 ], [ poison, %.Flow12_crit_edge ]
  %56 = phi i1 [ false, %Flow13 ], [ true, %.Flow12_crit_edge ]
  %57 = phi i1 [ %50, %Flow13 ], [ true, %.Flow12_crit_edge ]
  br i1 %57, label %loop.exit.guard, label %Flow12._crit_edge

Flow12._crit_edge:
  br label %2

loop.exit.guard1:
  br i1 %26, label %20, label %loop.exit.guard1.Flow13_crit_edge

loop.exit.guard1.Flow13_crit_edge:
  br label %Flow13

Flow9:
  %58 = phi i1 [ true, %45 ], [ false, %.Flow9_crit_edge ]
  %59 = phi i32 [ %46, %45 ], [ poison, %.Flow9_crit_edge ]
  %60 = phi i1 [ %47, %45 ], [ true, %.Flow9_crit_edge ]
  br label %Flow8

loop.exit.guard3:
  br i1 %41, label %32, label %loop.exit.guard3.Flow11_crit_edge

loop.exit.guard3.Flow11_crit_edge:
  br label %Flow11
}

declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1) #1
attributes #0 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #1 = { nounwind willreturn memory(argmem: read) }
!feme.cpu.resources = !{!0}
!feme.cpu.bound_resources = !{!1}
!0 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!1 = !{!"main", i32 1, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0}
