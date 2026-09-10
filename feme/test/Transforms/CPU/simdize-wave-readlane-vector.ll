; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A vector-typed `wave.readlane` (roadmap L89d): `RotateConversionPattern`
; converts `spirv.GroupNonUniformRotateKHR` straight to
; `llvm.spv.wave.readlane` at the SPIR-V op's own result type, so a
; `bvec2`/`ivec3`/`vec4` `subgroupClusteredRotate` reaches `SIMDizePass` as a
; genuine `<N x T>` gather. It decomposes into one `feme.cpu.wave.readlane`
; per component -- a gather is independent per component, since every
; component of output lane `L` reads the same source lane `I[L]` -- all
; sharing the single widened lane index, rather than one illegal
; `<W x <N x T>>` call. Here the divergent lane index makes the result
; divergent too, so each component stays a wide `<4 x i32>` for its
; downstream `extractelement` users.
;
; The two components' source values here are distinct (`%tid` in component 0,
; `%lane` in component 1), so each gather call's own operand identifies which
; component it lowers.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <2 x i32>>
; CHECK: %[[TID:.*]] = call <4 x i32> @feme.cpu.builtin.thread_id
; CHECK: %[[LANE:.*]] = call <4 x i32> @feme.cpu.builtin.lane_index
; CHECK: %[[IDX:.*]] = add <4 x i32> %[[LANE]], splat (i32 1)
; CHECK: call <4 x i32> @feme.cpu.wave.readlane.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %[[TID]], <4 x i32> %[[IDX]])
; CHECK: call <4 x i32> @feme.cpu.wave.readlane.i32.v4(<4 x i1> %wave_entry_mask, <4 x i32> %[[LANE]], <4 x i32> %[[IDX]])
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %rot = add i32 %lane, 1
  %v0 = insertelement <2 x i32> poison, i32 %tid, i32 0
  %v1 = insertelement <2 x i32> %v0, i32 %lane, i32 1
  %gathered = call <2 x i32> @llvm.spv.wave.readlane.v2i32(<2 x i32> %v1, i32 %rot)
  %e0 = extractelement <2 x i32> %gathered, i32 0
  %e1 = extractelement <2 x i32> %gathered, i32 1
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare <2 x i32> @llvm.spv.wave.readlane.v2i32(<2 x i32>, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
