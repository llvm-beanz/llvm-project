; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L433: a mask-affecting `feme.stage.output.store.f32` call before
; an otherwise-uniform loop forces `DiamondFlattener::run()`'s *commit*
; path to run (its decline-and-roll-back path, fixed by `L432(b)`, only
; applies when the whole function has *neither* a divergent branch *nor*
; any mask-affecting op) -- but that commit path's own whole-function walk
; passes straight through this loop's `header`/`check`/`Flow`/`exit` chain
; while merely discovering (not flattening) the loop's own cycle boundary,
; and used to never release those passed-through blocks back out of
; `MergeVisited` afterward. `LoopLinearizer`'s own later, entirely
; legitimate `flattenLoopBodyRegion` walk over that same chain (recovering
; this loop's uniform exit check via `recoverRelayExitCheck`'s
; `Flow`-dispatch-block case) then spuriously hit a stale "reached by more
; than one divergent root" conflict on those same blocks, misreported as
; "has an internal branch in a body shape this milestone does not yet
; lower". Fixed by having `DiamondFlattener::run()`'s commit path release
; its own visited blocks into `ResolvedBlocks`, exactly as its sibling
; `flattenLoopBodyDiamond`/`flattenLoopBodyRegion` methods already do.
; Block/value names and shape preserved verbatim (only renamed from their
; original numeric form) from the real failing case isolated via
; `FEME_DUMP_IR_PRELINEARIZE=1` against `dEQP-VK.synchronization.op.
; single_queue.fence.write_image_tess_eval_read_image_tess_control.
; image_128_r32_uint`'s compiled tessellation-domain-stage shader.

; CHECK-LABEL: define void @main(
; CHECK-NOT: unsupported
define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
entry:
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 1.000000e+00, i32 0)
  br label %header

header:                                           ; preds = %Flow._crit_edge, %entry
  %i = phi i32 [ 0, %entry ], [ %check.phi, %Flow._crit_edge ]
  br label %check

exit:                                             ; preds = %Flow
  ret void

Flow:                                             ; preds = %.Flow_crit_edge, %latch
  %check.phi = phi i32 [ %inc, %latch ], [ poison, %.Flow_crit_edge ]
  %check.cond = phi i1 [ false, %latch ], [ true, %.Flow_crit_edge ]
  br i1 %check.cond, label %exit, label %Flow._crit_edge

Flow._crit_edge:                                  ; preds = %Flow
  br label %header

latch:                                            ; preds = %body
  %inc = add i32 %i, 1
  br label %Flow

check:                                            ; preds = %header
  %cond = icmp slt i32 %i, 128
  br i1 %cond, label %body, label %.Flow_crit_edge

.Flow_crit_edge:                                  ; preds = %check
  br label %Flow

body:                                             ; preds = %check
  %loaded = call <4 x i32> @feme.cpu.image.load.1d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %i, i32 0, i1 true)
  call void @feme.cpu.image.store.1d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %i, <4 x i32> %loaded, i1 true)
  br label %latch
}

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare <4 x i32> @feme.cpu.image.load.1d.v4i32(ptr, i32, i32, i32, i32, i1) #2
declare void @feme.cpu.image.store.1d.v4i32(ptr, i32, i32, i32, <4 x i32>, i1) #1

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="domain" "feme.tessellation.domain"="triangle" "feme.tessellation.output_primitive"="triangle_ccw" "feme.tessellation.partitioning"="integer" "hlsl.shader"="domain" }
attributes #1 = { nounwind willreturn memory(argmem: write) }
attributes #2 = { nounwind willreturn memory(argmem: read) }
