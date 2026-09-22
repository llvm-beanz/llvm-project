; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; (Roadmap L148) `llvm.spv.wave.broadcast`'s lane index is spec-guaranteed
; uniform ("Id must come from a constant instruction" per
; `OpGroupNonUniformBroadcast`'s own spec text -- see
; `WaveCallKind::Broadcast`'s comment in WaveCalls.h), unlike
; `llvm.spv.wave.readlane`'s genuinely-varying-index `OpGroupNonUniform
; Shuffle` semantics (see wave-lowering-readlane-varying.ll). That lets
; `lowerBroadcast` (WaveLowering.cpp) collapse to exactly three
; instructions -- one guarded scalar extract, at lane 0 of the widened
; lane index -- instead of `lowerReadLane`'s `O(WaveSize)` per-lane loop:
; this `O(1)` shape is the actual fix for the `dEQP-VK.subgroups.
; ballot_broadcast.compute.*_requiredsubgroupsize{64,128}` hang, whose
; shader source's `N == WaveSize` broadcast call sites previously
; compounded `ReadLane`'s shared `O(WaveSize)`-per-site cost into
; `O(WaveSize^2)` total IR (see `L148` in feme/docs/VulkanCTSReport.md).

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[ACTIVE:.*]] = extractelement <4 x i1> %wave_entry_mask, i32 0
; CHECK-NEXT: %[[VAL:.*]] = extractelement <4 x i32> %tid1, i32 0
; CHECK-NEXT: select i1 %[[ACTIVE]], i32 %[[VAL]], i32 0
; CHECK-NOT: extractelement
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %val = call i32 @llvm.spv.wave.broadcast.i32(i32 %tid, i32 0)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.spv.wave.broadcast.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
