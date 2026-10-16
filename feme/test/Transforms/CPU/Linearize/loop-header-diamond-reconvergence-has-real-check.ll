; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L419(u): a reduction of
; `dEQP-VK.glsl.loops.special.do_while_dynamic_iterations.
; select_iteration_count_{fragment,vertex}` (see `agent_thoughts.md`'s own
; "L419(u)" session entry for the full root-cause writeup).
;
; `Header` (block `%12` below) has its own internal if/else (`%.not`,
; built from a `feme.cpu.resource.load.raw.i32` call whose binding/offset
; are the same for every lane, so the branch itself is genuinely
; lane-uniform) whose two arms (`%25`/`._crit_edge`) reconverge at block
; `%23` -- which itself computes the loop's own real, per-lane-divergent
; trip-count check (`%24 = icmp sge i32 %15, %.0`, where `%.0` is seeded
; from a per-lane input) before branching to the loop's own backedge/exit.
;
; `DiamondFlattener::isLoopControlEdgeThroughRelay` exists precisely to
; recognize that `%23` -- `Header`'s own natural reconvergence point --
; sits directly on the path to a genuine loop-control edge, so
; `DiamondFlattener::run()`'s whole-function walk must stop at `Header`
; and defer this cycle to `LoopLinearizer` rather than flattening
; `Header`'s internal if/else with its own, separate, pre-masking
; traversal. Before this fix, that relay chase gated on
; `isPureRelayBlock(Target)` *before* checking whether `Target` was the
; chase's own `StopAt` bound (`Header`'s immediate post-dominator) --
; wrongly disqualifying `%23` from the check entirely merely because it
; computes its own real `icmp` ahead of its branch, rather than being a
; pure phi-and-branch relay block like every other hop this chase walks
; through. The observable effect on this exact reduction is narrow (both
; the before- and after-fix IR already route this loop through
; `LoopLinearizer`'s correct masked-while-loop machinery, so no shader
; this session could find mis-renders either way): without the fix, the
; loop's single-iteration exit arm redundantly re-derives `live.merge`/
; `sideeffect.merge` as two-input phis of the literal constant `true`
; rather than folding them away during masking, and threads that
; unfolded phi into the exit block's per-channel masked stores and
; `feme.cpu.stage.return.masks.i1` call instead of the plain `true`
; constant seen below -- harmless today, but exactly the kind of stale,
; unnecessarily-symbolic mask this session's broader investigation (see
; `agent_thoughts.md`) found suspicious enough to want fixed before
; trusting any future change in this area. The fix reorders
; `isLoopControlEdgeThroughRelay` to check `Target == StopAt` before the
; `isPureRelayBlock` gate, letting `applyStageMasks`'s own masking of the
; one-iteration exit arm constant-fold trivially instead.

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
  %1 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %2 = fadd float %1, 5.000000e-01
  %3 = fptosi float %2 to i32
  %4 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 0, i32 0)
  %5 = insertelement <4 x float> poison, float %4, i64 0
  %6 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 1, i32 0)
  %7 = insertelement <4 x float> %5, float %6, i64 1
  %8 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 2, i32 0)
  %9 = insertelement <4 x float> %7, float %8, i64 2
  %10 = call float @feme.stage.input.load.f32(i32 1, i32 0, i32 3, i32 0)
  %11 = insertelement <4 x float> %9, float %10, i64 3
  br label %12

12:                                               ; preds = %._crit_edge1, %0
  %.08 = phi <4 x float> [ %11, %0 ], [ %14, %._crit_edge1 ]
  %.07 = phi i32 [ 0, %0 ], [ %15, %._crit_edge1 ]
  %13 = fadd <4 x float> %.08, splat (float 1.000000e+00)
  %14 = shufflevector <4 x float> %13, <4 x float> poison, <4 x i32> <i32 1, i32 2, i32 3, i32 0>
  %15 = add i32 %.07, 1
  %16 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)
  %.not = icmp ne i32 %16, 0
  br i1 %.not, label %25, label %._crit_edge

._crit_edge:                                      ; preds = %12
  br label %23

17:                                               ; preds = %23
  %18 = fadd <4 x float> %14, <float -3.000000e+00, float -3.000000e+00, float -3.000000e+00, float poison>
  %19 = insertelement <4 x float> %18, float 1.000000e+00, i64 3
  %20 = extractelement <4 x float> %19, i64 0
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float %20, i32 0)
  %21 = extractelement <4 x float> %19, i64 1
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 1, float %21, i32 0)
  %22 = extractelement <4 x float> %19, i64 2
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 2, float %22, i32 0)
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 3, float 1.000000e+00, i32 0)
  ret void

23:                                               ; preds = %._crit_edge, %25
  %.0 = phi i32 [ %27, %25 ], [ 0, %._crit_edge ]
  %24 = icmp sge i32 %15, %.0
  br i1 %24, label %17, label %._crit_edge1

._crit_edge1:                                     ; preds = %23
  br label %12

25:                                               ; preds = %12
  %26 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 0, i1 true)
  %27 = mul i32 %26, %3
  br label %23
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

; Function Attrs: nounwind willreturn memory(argmem: read)
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1) #1

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="fragment" "hlsl.shader"="pixel" }
attributes #1 = { nounwind willreturn memory(argmem: read) }

; CHECK-LABEL: define void @main(
; The loop's own real trip count is genuinely per-lane divergent, so it
; must become a masked while-loop (confirms `LoopLinearizer`, not
; `DiamondFlattener::run()`'s early whole-function walk, builds this
; loop's masking).
; CHECK: %active.live = phi i1 [ true, {{.*}} ], [ %active.check.live, {{.*}} ]
; Header's own internal if/else stays a real, unmasked branch (both arms
; are side-effect free, and the load is lane-uniform, so this is
; correct, not a reintroduced bug).
; CHECK: %.not = icmp ne i32 %{{[0-9]+}}, 0
; CHECK-NEXT: br i1 %.not, label %{{[0-9]+}}, label %._crit_edge
; The one-iteration exit arm's masks constant-fold to a plain `true`
; rather than surviving as an unfolded two-constant-input phi (the fix's
; actual observable effect on this reduction -- see file comment above).
; CHECK: call void @feme.cpu.masked.stage.output.store.f32(i32 2, i32 0, i32 0, float %{{[0-9]+}}, i32 0, i1 true)
; CHECK-NOT: %live.merge
; CHECK-NOT: %sideeffect.merge
; CHECK: %loop.any.active = call i1 @feme.cpu.mask.any(i1 %active.check.live)
; CHECK-NEXT: br i1 %loop.any.active, label %12, label %17
