; RUN: feme-opt --llvm -passes=feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L227(a)/L229 (feme/docs/Roadmap.md): `feme::cpu::SIMDizePass`'s own
; if-conversion (predicating a divergent `if`'s guard into a mask rather than
; a real branch) can leave a genuinely unreachable block behind -- a stale
; `..._crit_edge` landing pad (`.Flow7_crit_edge` below) that used to be a
; live successor before predication folded its guard away into
; `%live.t.wide`/`%sideeffect.t.wide` selects. This is not a surviving branch
; `feme::cpu::isLinearChain` needs to reason about (it is dead code, reached
; from nowhere), but `splitAtGroupSyncBarriers` used to reject it anyway,
; because `isLinearChain`'s own sanity check (every block in the function
; must be visited by the walk) counted it as an unexplained extra block. This
; is exactly the shape `dEQP-VK`/`offload-test-suite`'s
; `Feature/HLSLLib/Barrier.32.test` compiles to for each of its four
; `if (Index == 0) { for (uint I = 0; I < 64; ++I) ... }` blocks (this file
; keeps only the first). `splitAtGroupSyncBarriers` now prunes unreachable
; blocks (`EliminateUnreachableBlocks`) before `isLinearChain` runs, so this
; two-barrier, one-divergent-loop-arm wave body still splits into two
; regions instead of being diagnosed as unsupported.

; CHECK-NOT: .Flow7_crit_edge

; CHECK-LABEL: define internal void @main.region0(
; CHECK: call void @llvm.masked.scatter.v4i32.v4p0(

; CHECK-LABEL: define internal void @main.region1(
; CHECK: call void @feme.cpu.resource.store.raw.i32(
; CHECK-NOT: .Flow7_crit_edge

declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
declare <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr>, <4 x i1>, <4 x i32>)
declare void @llvm.masked.scatter.v4i32.v4p0(<4 x i32>, <4 x ptr>, <4 x i1>)
declare void @llvm.spv.group.memory.barrier.with.group.sync()

define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count, i32 %wave_group_id_x, i32 %wave_group_id_y, i32 %wave_group_id_z, i32 %wave_group_count_x, i32 %wave_group_count_y, i32 %wave_group_count_z, i32 %wave_index, <4 x i1> %wave_entry_mask, <4 x i1> %wave_sideeffect_mask, ptr %wave_groupshared) #0 {
  %1 = mul i32 %wave_index, 4
  %.splatinsert = insertelement <4 x i32> poison, i32 %1, i64 0
  %.splat = shufflevector <4 x i32> %.splatinsert, <4 x i32> poison, <4 x i32> zeroinitializer
  %2 = add <4 x i32> %.splat, <i32 0, i32 1, i32 2, i32 3>
  %3 = urem <4 x i32> %2, splat (i32 64)
  %.wide8 = add <4 x i32> %3, splat (i32 1)
  %GroupData.flat = getelementptr i8, ptr %wave_groupshared, i64 0
  %.wide9155 = getelementptr [64 x i32], ptr %GroupData.flat, i32 0, <4 x i32> %3
  call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> %.wide8, <4 x ptr> align 4 %.wide9155, <4 x i1> %wave_sideeffect_mask)
  call void @llvm.spv.group.memory.barrier.with.group.sync()
  %.wide10 = icmp eq <4 x i32> %3, zeroinitializer
  %not..wide = xor <4 x i1> %.wide10, splat (i1 true)
  %live.t.wide = and <4 x i1> splat (i1 true), %.wide10
  %sideeffect.t.wide = and <4 x i1> splat (i1 true), %.wide10
  %live.f.wide = and <4 x i1> splat (i1 true), %not..wide
  %sideeffect.f.wide = and <4 x i1> splat (i1 true), %not..wide
  br label %11

.Flow7_crit_edge:                                 ; No predecessors!
  br label %17

11:                                               ; preds = %Flow6._crit_edge, %0
  %.wide = phi <4 x i32> [ %.wide15, %Flow6._crit_edge ], [ splat (i32 1), %0 ]
  %12 = phi i32 [ %16, %Flow6._crit_edge ], [ 0, %0 ]
  %13 = icmp ult i32 %12, 64
  br i1 %13, label %Flow6._crit_edge, label %17

Flow6._crit_edge:                                 ; preds = %11
  %GroupData.flat156 = getelementptr i8, ptr %wave_groupshared, i64 0
  %14 = getelementptr [64 x i32], ptr %GroupData.flat156, i32 0, i32 %12
  %.splat.splatinsert157 = insertelement <4 x ptr> poison, ptr %14, i64 0
  %.splat.splat158 = shufflevector <4 x ptr> %.splat.splatinsert157, <4 x ptr> poison, <4 x i32> zeroinitializer
  %masked.mask = and <4 x i1> %wave_entry_mask, %live.t.wide
  %15 = call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> align 4 %.splat.splat158, <4 x i1> %masked.mask, <4 x i32> zeroinitializer)
  %16 = add i32 %12, 1
  %.splat.splatinsert11 = insertelement <4 x i32> poison, i32 %16, i64 0
  %.splat.splat12 = shufflevector <4 x i32> %.splat.splatinsert11, <4 x i32> poison, <4 x i32> zeroinitializer
  %.wide13 = icmp eq <4 x i32> %15, %.splat.splat12
  %.wide14 = select <4 x i1> %.wide13, <4 x i32> splat (i32 1), <4 x i32> zeroinitializer
  %.wide15 = and <4 x i32> %.wide, %.wide14
  br label %11

17:                                               ; preds = %.Flow7_crit_edge, %11
  %sideeffect.merge.wide170 = phi <4 x i1> [ poison, %.Flow7_crit_edge ], [ %sideeffect.t.wide, %11 ]
  %live.merge.wide168 = phi <4 x i1> [ poison, %.Flow7_crit_edge ], [ %live.t.wide, %11 ]
  %live.merge1.wide = select <4 x i1> %.wide10, <4 x i1> %live.merge.wide168, <4 x i1> %live.f.wide
  %sideeffect.merge2.wide = select <4 x i1> %.wide10, <4 x i1> %sideeffect.merge.wide170, <4 x i1> %sideeffect.f.wide
  %.linearized.wide = select <4 x i1> %.wide10, <4 x i32> %.wide, <4 x i32> splat (i32 1)
  %.wide16 = add <4 x i32> %3, splat (i32 65)
  %.wide17 = zext <4 x i32> %3 to <4 x i64>
  %.wide18 = mul <4 x i64> %.wide17, splat (i64 4)
  %resource.mask = and <4 x i1> %wave_sideeffect_mask, %sideeffect.merge2.wide
  %lane.mask = extractelement <4 x i1> %resource.mask, i32 0
  %lane.offset = extractelement <4 x i64> %.wide18, i32 0
  %lane.value = extractelement <4 x i32> %.wide16, i32 0
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %lane.offset, i32 %lane.value, i1 %lane.mask)
  %lane.mask19 = extractelement <4 x i1> %resource.mask, i32 1
  %lane.offset20 = extractelement <4 x i64> %.wide18, i32 1
  %lane.value21 = extractelement <4 x i32> %.wide16, i32 1
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %lane.offset20, i32 %lane.value21, i1 %lane.mask19)
  %lane.mask22 = extractelement <4 x i1> %resource.mask, i32 2
  %lane.offset23 = extractelement <4 x i64> %.wide18, i32 2
  %lane.value24 = extractelement <4 x i32> %.wide16, i32 2
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %lane.offset23, i32 %lane.value24, i1 %lane.mask22)
  %lane.mask25 = extractelement <4 x i1> %resource.mask, i32 3
  %lane.offset26 = extractelement <4 x i64> %.wide18, i32 3
  %lane.value27 = extractelement <4 x i32> %.wide16, i32 3
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %lane.offset26, i32 %lane.value27, i1 %lane.mask25)
  call void @llvm.spv.group.memory.barrier.with.group.sync()
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="64,1,1" }
