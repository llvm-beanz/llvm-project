; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L434: two nested *uniform* loops, each independently containing an
; image load/store pair, with no mask-affecting op anywhere in the function.
; `LoopLinearizer` processes cycles post-order (innermost first); once the
; inner loop is confirmed fully uniform, `foldRedundantFlowBlocksInCycle`
; collapses its now-provably-redundant `inner.flow` dispatch block, rewiring
; `inner.flow.crit.edge` (the inner loop's own exit arm) to branch straight
; to `inner.exit` instead. `CycleInfo` (computed once per function, before
; `LoopLinearizer` ever runs -- unlike `DT`/`PDT`, recalculated before every
; `linearizeCycle` call) has no way to see that this rewire severed
; `inner.flow.crit.edge`'s own path back to the inner loop's header: it is
; still dominated by that header exactly as before (dominance only reasons
; forward from the entry, unaffected by a successor edge changing further
; down), so `CI.contains` keeps reporting it a member of the inner cycle.
; When the *outer* loop's own body-region precheck later walks through
; `inner.check` (the inner loop's own exit check, unmutated itself) its
; "exit" edge to `inner.flow.crit.edge` is misread as an ordinary in-cycle
; edge rather than a cycle boundary, so `validate` tries to flatten
; `inner.check` as an ordinary two-way diamond -- computing its immediate
; post-dominator as `inner.flow.crit.edge` itself (correct: every path back
; through the now-collapsed inner loop must still pass through it before
; reaching the function's one `ret`), which then collides with
; `inner.flow.crit.edge` also being one of `inner.check`'s own two successors,
; tripping the "empty diamond arm" diagnostic. Fixed by having
; `isLoopControlEdge` re-verify a `CI.contains` "still a member" answer with
; a live, `CI`-membership-bounded reachability check (mirroring the already-
; live `DT`/`PDT` recompute's own freshness, rather than trusting `CI`'s
; one-time, now-stale snapshot) before accepting it. Block/value names and
; shape preserved verbatim (only renamed from their original numeric form,
; matching `loop-body-resolved-blocks-after-mask-op.ll`'s own convention) from
; the real failing case isolated via `FEME_DUMP_IR_PRELINEARIZE=1` against
; `dEQP-VK.synchronization.op.single_queue.barrier.write_image_compute_
; read_image_compute.image_128x128_r16_uint`'s compiled compute-stage shader.

; CHECK-LABEL: define void @main(
; CHECK-NOT: unsupported
define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count) #0 {
  br label %outer.header

outer.header:                                     ; preds = %outer.flow.crit.edge, %0
  %outer.iv = phi i32 [ 0, %0 ], [ %outer.flow.phi, %outer.flow.crit.edge ]
  br label %outer.check

outer.exit:                                       ; preds = %outer.flow
  ret void

outer.latch:                                      ; preds = %inner.exit
  %outer.inc = add i32 %outer.iv, 1
  br label %outer.flow

outer.check:                                      ; preds = %outer.header
  %outer.cond = icmp slt i32 %outer.iv, 128
  br i1 %outer.cond, label %outer.body, label %.outer.flow.crit.edge

.outer.flow.crit.edge:                            ; preds = %outer.check
  br label %outer.flow

outer.body:                                       ; preds = %outer.check
  br label %inner.header

outer.flow:                                       ; preds = %.outer.flow.crit.edge, %outer.latch
  %outer.flow.phi = phi i32 [ %outer.inc, %outer.latch ], [ poison, %.outer.flow.crit.edge ]
  %outer.flow.cond = phi i1 [ false, %outer.latch ], [ true, %.outer.flow.crit.edge ]
  br i1 %outer.flow.cond, label %outer.exit, label %outer.flow.crit.edge

outer.flow.crit.edge:                             ; preds = %outer.flow
  br label %outer.header

inner.header:                                     ; preds = %inner.flow.crit.edge, %outer.body
  %inner.iv = phi i32 [ %inner.flow.phi, %inner.flow.crit.edge ], [ 0, %outer.body ]
  br label %inner.check

inner.exit:                                       ; preds = %inner.flow
  br label %outer.latch

inner.flow:                                       ; preds = %.inner.flow.crit.edge, %inner.latch
  %inner.flow.phi = phi i32 [ %inner.inc, %inner.latch ], [ poison, %.inner.flow.crit.edge ]
  %inner.flow.cond = phi i1 [ false, %inner.latch ], [ true, %.inner.flow.crit.edge ]
  br i1 %inner.flow.cond, label %inner.exit, label %inner.flow.crit.edge

inner.flow.crit.edge:                             ; preds = %inner.flow
  br label %inner.header

inner.latch:                                      ; preds = %inner.body
  %inner.inc = add i32 %inner.iv, 1
  br label %inner.flow

inner.check:                                      ; preds = %inner.header
  %inner.cond = icmp slt i32 %inner.iv, 128
  br i1 %inner.cond, label %inner.body, label %.inner.flow.crit.edge

.inner.flow.crit.edge:                            ; preds = %inner.check
  br label %inner.flow

inner.body:                                       ; preds = %inner.check
  %coord0 = insertelement <2 x i32> poison, i32 %inner.iv, i32 0
  %coord1 = insertelement <2 x i32> %coord0, i32 %outer.iv, i32 1
  %x = extractelement <2 x i32> %coord1, i64 0
  %y = extractelement <2 x i32> %coord1, i64 1
  %loaded = call <4 x i32> @feme.cpu.image.load.2d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %x, i32 %y, i32 0, i32 0, i1 true)
  call void @feme.cpu.image.store.2d.v4i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %x, i32 %y, <4 x i32> %loaded, i1 true)
  br label %inner.latch
}

declare <4 x i32> @feme.cpu.image.load.2d.v4i32(ptr, i32, i32, i32, i32, i32, i32, i1)
declare void @feme.cpu.image.store.2d.v4i32(ptr, i32, i32, i32, i32, <4 x i32>, i1)

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="compute" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
