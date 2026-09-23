; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; (Roadmap L148, index-source fixed by L152) `llvm.spv.wave.broadcast`'s
; lane index is spec-guaranteed uniform ("Id must come from a constant
; instruction" per `OpGroupNonUniformBroadcast`'s own spec text -- see
; `WaveCallKind::Broadcast`'s comment in WaveCalls.h), unlike
; `llvm.spv.wave.readlane`'s genuinely-varying-index `OpGroupNonUniform
; Shuffle` semantics (see wave-lowering-readlane-varying.ll). That lets
; `lowerBroadcast` (WaveLowering.cpp) collapse to a handful of
; instructions -- reading the index from the first lane the wave's own
; mask marks active (`getClampedFirstActiveLaneIndex`, the same "any
; active lane will do" helper `lowerAllEqual` already uses; see `L152`'s
; own writeup in `feme/docs/VulkanCTSReport.md` for why a hardcoded lane 0
; is not always active-lane-safe once `SIMDizePass::widenWaveCall` starts
; narrowing this call's mask to a divergent region narrower than the
; whole wave) -- instead of `lowerReadLane`'s `O(WaveSize)` per-lane loop:
; this `O(1)` shape is the actual fix for the `dEQP-VK.subgroups.
; ballot_broadcast.compute.*_requiredsubgroupsize{64,128}` hang, whose
; shader source's `N == WaveSize` broadcast call sites previously
; compounded `ReadLane`'s shared `O(WaveSize)`-per-site cost into
; `O(WaveSize^2)` total IR (see `L148` in feme/docs/VulkanCTSReport.md).
; This call has no enclosing divergent region (no `"feme.divergence.mask"`
; bundle), so its mask is the wave's own whole, unnarrowed entry mask --
; `getClampedFirstActiveLaneIndex` still resolves that to lane 0 here
; (this shader's own entry mask is never partial), matching the plain
; hardcoded-lane-0 case this test originally covered.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[MASKINT:.*]] = bitcast <4 x i1> %wave_entry_mask to i4
; CHECK: %[[ISZERO:.*]] = icmp eq i4 %[[MASKINT]], 0
; CHECK: %[[CTTZ:.*]] = call i4 @llvm.cttz.i4(i4 %[[MASKINT]], i1 false)
; CHECK: %[[ANYLANE:.*]] = select i1 %[[ISZERO]], i4 0, i4 %[[CTTZ]]
; CHECK: %[[ANYLANE32:.*]] = zext i4 %[[ANYLANE]] to i32
; CHECK: %[[SRCIDX:.*]] = extractelement <4 x i32> zeroinitializer, i32 %[[ANYLANE32]]
; CHECK: %[[ACTIVE:.*]] = extractelement <4 x i1> %wave_entry_mask, i32 %[[SRCIDX]]
; CHECK: %[[VAL:.*]] = extractelement <4 x i32> %tid1, i32 %[[SRCIDX]]
; CHECK: select i1 %[[ACTIVE]], i32 %[[VAL]], i32 0
; CHECK-NOT: extractelement
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %val = call i32 @llvm.spv.wave.broadcast.i32(i32 %tid, i32 0)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.spv.wave.broadcast.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
