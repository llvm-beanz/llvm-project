; RUN: feme-opt --llvm -passes=feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L425(a) (feme/docs/Roadmap.md): a header phi whose own recurrence
; feeds back itself unchanged (`%mask` below) is loop-invariant, not a
; genuine per-iteration induction. `feme::cpu::SIMDizePass` produces this
; shape for a per-lane divergence mask computed by an `if` ahead of a
; reduction loop that itself never re-diverges (e.g. the
; `Feature/HLSLLib/AllMemoryBarrierWithGroupSync.test`/
; `DeviceMemoryBarrierWithGroupSync.test` pattern: a single-lane guard
; computed once before a stride-halving loop whose own barrier-guarded body
; reads, but never updates, that guard's mask). `matchLoopShape` used to
; treat any header-residing recurrence identically to a genuine scalar
; induction, requiring a compile-time-constant initial value -- which a
; per-lane mask's own (non-constant, vector-typed) initial value never is,
; so the whole function fell through to `splitAtGroupSyncBarriers`'s
; unsupported "barrier inside non-linear control flow" diagnostic instead of
; ever being recognized by `matchLoopShape`. Such a phi is now recognized
; and threaded through the per-wave spill array like a genuine
; wave-persistent induction (roadmap H159(b)), except with no store-back:
; its slot's value provably never changes once seeded, so only a seed and
; reloads are needed.
;
; This also exercises the companion fix to the seed store's own insertion
; point: the prefix region here computes `%live.merge.wide` *before* the
; `i1`/vector-mask arithmetic that produces it, and the seed must land
; immediately after that definition rather than unconditionally at the very
; end of the (potentially barrier-split) prefix chain, or it could be
; stranded in a different outlined `.prefixN` region than the value it
; reads.

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

; The body region reloads the carried mask and never stores a new value
; back into the same slot: it is loop-invariant.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: %mask.reload.val = load <4 x i1>, ptr %mask.reload
; CHECK: call void @feme.cpu.resource.store.raw.i32(
; CHECK-NOT: store <4 x i1> {{.*}}, ptr %mask.carry

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count, i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z, i32 %wave_group_count_x, i32 %wave_group_count_y, i32 %wave_group_count_z, i32 %wave_index, <4 x i1> %wave_entry_mask, <4 x i1> %wave_sideeffect_mask, ptr %wave_groupshared) #0 {
  %tid = mul i32 %wave_index, 4
  %is.last.lane = icmp eq i32 %tid, 508
  %live.merge.wide = select i1 %is.last.lane, <4 x i1> splat (i1 true), <4 x i1> %wave_sideeffect_mask
  br label %header

header:
  %i = phi i32 [ 0, %0 ], [ %i.next, %body ]
  %mask = phi <4 x i1> [ %live.merge.wide, %0 ], [ %mask, %body ]
  %cond = icmp ult i32 %i, 512
  br i1 %cond, label %body, label %exit

body:
  %masked = and <4 x i1> %wave_sideeffect_mask, %mask
  %lane.mask = extractelement <4 x i1> %masked, i32 0
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i32 1, i1 %lane.mask)
  call void @llvm.spv.all.memory.barrier.with.group.sync()
  %i.next = add i32 %i, 1
  br label %header

exit:
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="512,1,1" }
