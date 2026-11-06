; RUN: feme-opt --llvm -passes="function(instnamer),feme-cpu-linearize" -S %s | FileCheck %s

; Roadmap ENV6(c)(5)/ENV6(c)(5a): a real pre-`feme-cpu-linearize` capture of
; `dEQP-VK.graphicsfuzz.cov-function-infinite-loop-return-identical-
; condition`. The outer loop (header `bb7`) has no exit of its own; its only
; exit is `loop.exit.guard`, the divergent branch reached *after* the inner
; loop (header `9`) finishes. That branch both leaves the outer loop and
; feeds its latch, which used to make `canFlattenLoopBodyRegion` reject the
; body outright (`Flow2` was reached both by the inner body diamond's own
; fallthrough and by the inner loop header's own exit-relay edge). Fixed by
; `ENV6(c)(5a)`'s mask-merging phi at the shared `Flow2` tail block, which
; threads the header's own active masks (`%active.live`/`%active.sideeffect`,
; live into `bb8` on the relay edge) together with the inner body diamond's
; own per-arm merge masks (`%live.merge51`/`%sideeffect.merge52`, live via
; `Flow3`), instead of silently reusing the diamond's own merge masks
; unconditionally for every predecessor into `Flow2` (which produced
; "Instruction does not dominate all uses" verifier failures before this
; milestone, the reason a plain `validate()`-only relaxation alone was never
; sufficient -- see `isRelayBypassMergeBlock`'s own comment in
; `Linearize.cpp`).

; CHECK: bb8:
; CHECK-NEXT: %.03 = phi i32 [ %i14, %Flow2._crit_edge ], [ %i7, %bb6 ]
; CHECK-NEXT: %active.live = phi i1 [ true, %bb6 ], [ %live.relay.merge, %Flow2._crit_edge ]
; CHECK-NEXT: %active.sideeffect = phi i1 [ true, %bb6 ], [ %sideeffect.relay.merge, %Flow2._crit_edge ]

; CHECK: Flow2:
; CHECK-NEXT: %i14 = phi i32 [ %i18, %Flow3 ], [ poison, %.Flow2_crit_edge ]
; CHECK-NEXT: %i15 = phi i1 [ false, %Flow3 ], [ true, %.Flow2_crit_edge ]
; CHECK-NEXT: %i16 = phi i1 [ %i46.linearized, %Flow3 ], [ true, %.Flow2_crit_edge ]
; CHECK-NEXT: %live.relay.merge = phi i1 [ %live.merge51, %Flow3 ], [ %active.live, %.Flow2_crit_edge ]
; CHECK-NEXT: %sideeffect.relay.merge = phi i1 [ %sideeffect.merge52, %Flow3 ], [ %active.sideeffect, %.Flow2_crit_edge ]


@spirv_var_21.str = private constant [13 x i8] c"spirv_var_21\00"
@spirv_var_40.str = private constant [13 x i8] c"spirv_var_40\00"
@_GLF_color = external addrspace(8) global <4 x float>, !spirv.Decorations !1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #0

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #1 !feme.signature !2 {
  %.0.unmerged.in11 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %1 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %2 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %3 = icmp sgt i32 %1, %2
  br i1 %3, label %._crit_edge, label %.Flow4_crit_edge

.Flow4_crit_edge:                                 ; preds = %0
  br label %Flow4

._crit_edge:                                      ; preds = %0
  br label %7

4:                                                ; preds = %Flow4
  %5 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %6 = icmp ne i32 %20, %5
  br i1 %6, label %41, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %4
  br label %Flow

7:                                                ; preds = %loop.exit.guard._crit_edge, %._crit_edge
  %8 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  br label %9

9:                                                ; preds = %Flow2._crit_edge, %7
  %.03 = phi i32 [ %15, %Flow2._crit_edge ], [ %8, %7 ]
  %10 = icmp sgt i32 %.03, 0
  br i1 %10, label %11, label %.Flow2_crit_edge

.Flow2_crit_edge:                                 ; preds = %9
  br label %Flow2

11:                                               ; preds = %9
  %12 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %13 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %14 = icmp sle i32 %12, %13
  br i1 %14, label %18, label %.Flow3_crit_edge

.Flow3_crit_edge:                                 ; preds = %11
  br label %Flow3

Flow2:                                            ; preds = %.Flow2_crit_edge, %Flow3
  %15 = phi i32 [ %46, %Flow3 ], [ poison, %.Flow2_crit_edge ]
  %16 = phi i1 [ false, %Flow3 ], [ true, %.Flow2_crit_edge ]
  %17 = phi i1 [ %47, %Flow3 ], [ true, %.Flow2_crit_edge ]
  %.inv = xor i1 %16, true
  br i1 %17, label %loop.exit.guard, label %Flow2._crit_edge

Flow2._crit_edge:                                 ; preds = %Flow2
  br label %9

18:                                               ; preds = %11
  %19 = add nsw i32 %.03, -1
  br label %Flow3

Flow4:                                            ; preds = %.Flow4_crit_edge, %"func(.exit"
  %20 = phi i32 [ %.0.unmerged.in2, %"func(.exit" ], [ %.0.unmerged.in11, %.Flow4_crit_edge ]
  br label %4

"func(.exit":                                     ; preds = %loop.exit.guard
  %.0.unmerged.in2 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  br label %Flow4

21:                                               ; preds = %Flow._crit_edge, %28
  %storemerge = phi <4 x float> [ %26, %Flow._crit_edge ], [ %40, %28 ]
  %22 = extractelement <4 x float> %storemerge, i64 0
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 0, float %22, i32 0)
  %23 = extractelement <4 x float> %storemerge, i64 1
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 1, float %23, i32 0)
  %24 = extractelement <4 x float> %storemerge, i64 2
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 2, float %24, i32 0)
  %25 = extractelement <4 x float> %storemerge, i64 3
  call void @feme.stage.output.store.f32(i32 0, i32 0, i32 3, float %25, i32 0)
  ret void

Flow:                                             ; preds = %.Flow_crit_edge, %41
  %26 = phi <4 x float> [ %45, %41 ], [ poison, %.Flow_crit_edge ]
  %27 = phi i1 [ false, %41 ], [ true, %.Flow_crit_edge ]
  br i1 %27, label %28, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %21

28:                                               ; preds = %Flow
  %29 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %30 = sitofp i32 %29 to float
  %31 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %32 = sitofp i32 %31 to float
  %33 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %34 = sitofp i32 %33 to float
  %35 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %36 = sitofp i32 %35 to float
  %37 = insertelement <4 x float> poison, float %30, i64 0
  %38 = insertelement <4 x float> %37, float %32, i64 1
  %39 = insertelement <4 x float> %38, float %34, i64 2
  %40 = insertelement <4 x float> %39, float %36, i64 3
  br label %21

41:                                               ; preds = %4
  %42 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 16, i1 true)
  %43 = sitofp i32 %42 to float
  %44 = insertelement <4 x float> poison, float %43, i64 0
  %45 = shufflevector <4 x float> %44, <4 x float> poison, <4 x i32> zeroinitializer
  br label %Flow

Flow3:                                            ; preds = %.Flow3_crit_edge, %18
  %46 = phi i32 [ %19, %18 ], [ poison, %.Flow3_crit_edge ]
  %47 = phi i1 [ false, %18 ], [ true, %.Flow3_crit_edge ]
  br label %Flow2

loop.exit.guard:                                  ; preds = %Flow2
  br i1 %.inv, label %"func(.exit", label %loop.exit.guard._crit_edge

loop.exit.guard._crit_edge:                       ; preds = %loop.exit.guard
  br label %7
}

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #2

attributes #0 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #1 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #2 = { nounwind willreturn memory(argmem: read) }

!feme.cpu.resources = !{!3}
!feme.cpu.bound_resources = !{!4}

!0 = !{i32 30, i32 0}
!1 = !{!0}
!2 = !{[116 x i8] c"\09\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
!3 = !{!"main", i32 0, i1 false, i32 0, i32 0, i32 0}
!4 = !{!"main", i32 2, i32 0, i32 0, i32 0, i32 0, i32 1, i32 0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 0}
