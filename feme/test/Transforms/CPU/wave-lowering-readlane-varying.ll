; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A genuinely varying lane index (here, each lane reads its mirror image
; across the wave: lane `L` reads source lane `3 - L`) lowers to a real
; per-lane gather -- see wave-lowering-readlane.ll's comment and
; WaveLowering.cpp's `lowerReadLane` -- rather than the single-extraction
; shortcut a uniform index would allow. Each of the four output lanes reads
; its own (different) source index computed from the widened, per-lane
; `%rev` operand, not a shared one. Uses `llvm.spv.wave.readlane` (SPIR-V's
; shuffle-style semantics genuinely permit a varying index -- see
; `feme::cpu::WaveTTIImpl::getValueUniformity`'s comment) rather than
; `llvm.dx.wave.readlane`, so `feme::cpu::FunctionWidener::widenWaveCall`
; classifies this call itself divergent and keeps the whole gather result
; wide, instead of narrowing it back to one (HLSL-uniform-by-construction)
; scalar lane the way `wave-lowering-readlane.ll`'s DXIL instance does; the
; `dx.wave.getlaneindex` source of `%rev` is only there to give each lane a
; distinct index; it does not need to be the same source format.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[IDX0:.*]] = extractelement <4 x i32> %rev.wide, i32 0
; CHECK: extractelement <4 x i1> %wave_entry_mask, i32 %[[IDX0]]
; CHECK: extractelement <4 x i32> %tid1, i32 %[[IDX0]]
; CHECK: %[[IDX1:.*]] = extractelement <4 x i32> %rev.wide, i32 1
; CHECK: extractelement <4 x i1> %wave_entry_mask, i32 %[[IDX1]]
; CHECK: extractelement <4 x i32> %tid1, i32 %[[IDX1]]
; CHECK: %[[IDX2:.*]] = extractelement <4 x i32> %rev.wide, i32 2
; CHECK: extractelement <4 x i1> %wave_entry_mask, i32 %[[IDX2]]
; CHECK: extractelement <4 x i32> %tid1, i32 %[[IDX2]]
; CHECK: %[[IDX3:.*]] = extractelement <4 x i32> %rev.wide, i32 3
; CHECK: extractelement <4 x i1> %wave_entry_mask, i32 %[[IDX3]]
; CHECK: extractelement <4 x i32> %tid1, i32 %[[IDX3]]
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %rev = sub i32 3, %lane
  %val = call i32 @llvm.spv.wave.readlane.i32(i32 %tid, i32 %rev)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare i32 @llvm.spv.wave.readlane.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
