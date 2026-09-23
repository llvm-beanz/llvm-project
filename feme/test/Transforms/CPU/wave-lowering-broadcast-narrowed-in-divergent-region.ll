; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L152: `subgroupBroadcast`/`OpGroupNonUniformBroadcast` (unlike
; `Ballot`'s predicate or a reduce's value operand) has no operand of its
; own for a divergent region to narrow -- its `Id` operand identifies the
; *source lane*, not "is the current lane active" -- so
; `feme::cpu::LinearizePass` attaches the region's own live mask as a
; `"feme.divergence.mask"` operand bundle instead (mirroring
; `WaveIsFirstLane`'s own, pre-existing treatment; see
; wave-lowering-is-first-lane.ll), for `feme::cpu::FunctionWidener::
; widenWaveCall` to AND into this call's own `WideMask` once widened.
;
; This is a reduction of `dEQP-VK.subgroups.ballot_broadcast.compute.
; subgroupbroadcast_nonconst_*`'s own "lane id that is only uniform across
; active lanes" case: a `subgroupBroadcast` inside `if (tid >= 2)`, whose
; own index (`tid & ~1`, uniform across the two lanes that take this arm,
; but not across the whole 4-lane wave) previously had its "any active
; lane will do" source-lane lookup (`feme::cpu::WaveLoweringPass::
; lowerBroadcast`'s own `getClampedFirstActiveLaneIndex` call) resolve
; against the wave's whole, unnarrowed entry mask instead of this `if`'s
; own narrower live mask, so it could pick an inactive lane (whichever the
; *whole wave's* entry mask happened to mark first-active, entirely
; unrelated to this specific call) and silently return the wrong value
; (typically 0, since that wrong lane's own mask bit then read `false`) --
; see "L152" in feme/docs/VulkanCTSReport.md for the full root-cause
; writeup and CTS re-run.

; CHECK-LABEL: define void @main(
; CHECK: if.true:
; CHECK: %wave.divergence.masked = and <4 x i1> %wave_entry_mask, %live.t.wide
; CHECK: %[[MASKINT:.*]] = bitcast <4 x i1> %wave.divergence.masked to i4
; CHECK: %[[ISZERO:.*]] = icmp eq i4 %[[MASKINT]], 0
; CHECK: %[[CTTZ:.*]] = call i4 @llvm.cttz.i4(i4 %[[MASKINT]], i1 false)
; CHECK: %[[ANYLANE:.*]] = select i1 %[[ISZERO]], i4 0, i4 %[[CTTZ]]
; CHECK: %[[ANYLANE32:.*]] = zext i4 %[[ANYLANE]] to i32
; CHECK: %[[SRCIDX:.*]] = extractelement <4 x i32> %id.wide, i32 %[[ANYLANE32]]
; CHECK: %[[ACTIVE:.*]] = extractelement <4 x i1> %wave.divergence.masked, i32 %[[SRCIDX]]
; CHECK: %[[VAL:.*]] = extractelement <4 x i32> %tid1, i32 %[[SRCIDX]]
; CHECK: select i1 %[[ACTIVE]], i32 %[[VAL]], i32 0
define void @main(ptr %heap, i32 %heap_count) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp uge i32 %tid, 2
  br i1 %c, label %if.true, label %if.false
if.false:
  br label %end
if.true:
  %id = and i32 %tid, -2
  %bc = call i32 @llvm.spv.wave.broadcast.i32(i32 %tid, i32 %id)
  br label %end
end:
  %v = phi i32 [%bc, %if.true], [0, %if.false]
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.spv.wave.broadcast.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
