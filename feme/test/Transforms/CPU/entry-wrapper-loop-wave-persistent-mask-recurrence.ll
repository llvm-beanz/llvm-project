; RUN: feme-opt --llvm -passes=feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L426 (feme/docs/Roadmap.md): a header phi whose own recurrence
; genuinely changes every iteration (not merely `L425(a)`'s loop-invariant
; "feeds back itself unchanged" case) may still carry a non-constant,
; wave-lane-specific *seed* -- e.g. a per-lane divergence mask that both
; starts from a value computed ahead of the loop (not a compile-time
; constant) and keeps narrowing every iteration inside the loop's own
; collapsed body+latch block, before that block's own barrier
; (`Feature/HLSLLib/AllMemoryBarrierWithGroupSync.test`'s own
; `GroupCounter`-incrementing loop produces exactly this shape).
; `matchLoopShape` used to require a compile-time-constant seed for any
; such induction *unless* its recurrence fed back the header phi itself
; unchanged (`L425(a)`'s own narrower case) -- bailing out for this
; genuinely-recurring-but-non-constant-seeded case even though
; `spillValuesLiveAcrossBarriers`'s existing per-wave seed/store-back
; machinery (roadmap H159(b)) already supports it without any further
; change: the seed still lands immediately after its own definition (same
; as `L425(a)`), and the store-back (skipped only when the recurrence is
; exactly the header phi itself) now fires, since the mask here really
; does change each iteration.

declare void @llvm.spv.all.memory.barrier.with.group.sync()
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)

; CHECK: %main.barrier_spill = type { <4 x i1> }

; The prefix region seeds every wave's slot with the mask's own,
; non-constant initial value right after that value is computed.
; CHECK-LABEL: define internal void @main.prefix0(
; CHECK: %live.merge.wide = select i1 %is.last.lane, <4 x i1> splat (i1 true), <4 x i1> %wave_sideeffect_mask
; CHECK-NEXT: %mask.init.slot = getelementptr %main.barrier_spill, ptr %barrier_spill, i32 %wave_index
; CHECK-NEXT: %mask.init = getelementptr {{.*}} %main.barrier_spill, ptr %mask.init.slot, i32 0, i32 0
; CHECK-NEXT: store <4 x i1> %live.merge.wide, ptr %mask.init

; The body region reloads the carried mask at the top of its own barrier
; region (ahead of the use), narrows it, and -- unlike `L425(a)`'s
; loop-invariant case -- stores the newly-narrowed value back into the
; very same slot right after it is computed, so the next iteration's
; reload observes it.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: %mask.reload.val = load <4 x i1>, ptr %mask.reload
; CHECK: call void @feme.cpu.resource.store.raw.i32(
; CHECK: %mask.next = select i1 %narrow, <4 x i1> %mask.reload.val, <4 x i1> zeroinitializer
; CHECK: %mask.carry = getelementptr {{.*}} %main.barrier_spill, ptr %{{.*}}, i32 0, i32 0
; CHECK-NEXT: store <4 x i1> %mask.next, ptr %mask.carry

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count, i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z, i32 %wave_group_count_x, i32 %wave_group_count_y, i32 %wave_group_count_z, i32 %wave_index, <4 x i1> %wave_entry_mask, <4 x i1> %wave_sideeffect_mask, ptr %wave_groupshared) #0 {
  %tid = mul i32 %wave_index, 4
  %is.last.lane = icmp eq i32 %tid, 508
  %live.merge.wide = select i1 %is.last.lane, <4 x i1> splat (i1 true), <4 x i1> %wave_sideeffect_mask
  br label %header

header:
  %i = phi i32 [ 0, %0 ], [ %i.next, %body ]
  %mask = phi <4 x i1> [ %live.merge.wide, %0 ], [ %mask.next, %body ]
  %cond = icmp ult i32 %i, 512
  br i1 %cond, label %body, label %exit

body:
  %lane.mask = extractelement <4 x i1> %mask, i32 0
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 1, i1 %lane.mask)
  %narrow = icmp ult i32 %i, 256
  %mask.next = select i1 %narrow, <4 x i1> %mask, <4 x i1> zeroinitializer
  call void @llvm.spv.all.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  br label %header

exit:
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="512,1,1" }
