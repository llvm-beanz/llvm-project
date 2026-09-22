; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A vector-typed `wave.broadcast` (roadmap L148): `BroadcastConversionPattern`
; (SPIRVToLLVMPatterns.cpp) converts `spirv.GroupNonUniformBroadcast`
; straight to `llvm.spv.wave.broadcast` at the SPIR-V op's own result type,
; so a `bvec2`/`ivec3`/`vec4` `subgroupBroadcast` reaches `SIMDizePass` as a
; genuine `<N x T>` call, the `Broadcast` counterpart of
; simdize-wave-readlane-vector.ll's `ReadLane` case. Unlike that `ReadLane`
; gather, `Broadcast`'s lane index is spec-guaranteed uniform, so each
; per-component `feme.cpu.wave.broadcast` call's own result is a plain
; scalar `T` -- reassembled directly into the `<2 x i32>` vector `%bc`'s
; users expect -- rather than staying a wide `<4 x T>` component pending a
; divergence check the way `ReadLane`'s decomposition does.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <2 x i32>>
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id
; CHECK: %[[LANE:.*]] = call <4 x i32> @feme.cpu.builtin.lane_index
; CHECK: %[[C0:.*]] = call i32 @feme.cpu.wave.broadcast.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %[[TID]], <4 x i32> zeroinitializer)
; CHECK: insertelement <2 x i32> poison, i32 %[[C0]], i32 0
; CHECK: %[[C1:.*]] = call i32 @feme.cpu.wave.broadcast.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %[[LANE]], <4 x i32> zeroinitializer)
; CHECK: insertelement <2 x i32> %{{.*}}, i32 %[[C1]], i32 1
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %v0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %v1 = insertelement <2 x i32> %v0, i32 %lane, i32 1
  %bc = call <2 x i32> @llvm.spv.wave.broadcast.v2i32(<2 x i32> %v1, i32 0)
  %e0 = extractelement <2 x i32> %bc, i32 0
  %e1 = extractelement <2 x i32> %bc, i32 1
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare <2 x i32> @llvm.spv.wave.broadcast.v2i32(<2 x i32>, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
