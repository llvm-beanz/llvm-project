; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=16 -S %s | FileCheck %s
; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave -feme-cpu-wave-size=8 -S %s | FileCheck %s --check-prefix=NARROW

; At or above `ReadLaneMemoryGatherMinWaveSize` a `WaveReadLaneAt` gathers
; through entry-block scratch memory rather than through a per-lane dynamic
; `extractelement` (see `lowerReadLaneViaMemory`): the mask is widened to a
; byte per lane, the source vector is stored once, and each output lane's
; source index -- masked to `W - 1` so it can never leave the scratch
; array -- becomes a real `getelementptr`/`load`. A narrower wave keeps the
; original straight-line vector form. Uses the same mirrored, genuinely
; varying lane index as wave-lowering-readlane-varying.ll (lane `L` reads
; source lane `W - 1 - L`), since a uniform index constant-folds the wrap
; mask away.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.wave
; CHECK: %[[MASKA:.*]] = alloca [16 x i8]
; CHECK: %[[SRCA:.*]] = alloca [16 x i32]
; CHECK: %[[DSTA:.*]] = alloca [16 x i32]
; CHECK: %[[MASKB:.*]] = zext <16 x i1> %wave_entry_mask to <16 x i8>
; CHECK: store <16 x i8> %[[MASKB]], ptr %[[MASKA]]
; CHECK: store <16 x i32> %tid1, ptr %[[SRCA]]
; CHECK: %[[IDX0:.*]] = extractelement <16 x i32> %rev.wide, i32 0
; CHECK: %[[WRAP0:.*]] = and i32 %[[IDX0]], 15
; CHECK: %[[MP0:.*]] = getelementptr inbounds i8, ptr %[[MASKA]], i32 %[[WRAP0]]
; CHECK: %[[MB0:.*]] = load i8, ptr %[[MP0]]
; CHECK: %[[ACT0:.*]] = trunc i8 %[[MB0]] to i1
; CHECK: %[[SP0:.*]] = getelementptr inbounds i32, ptr %[[SRCA]], i32 %[[WRAP0]]
; CHECK: %[[V0:.*]] = load i32, ptr %[[SP0]]
; CHECK: %[[SEL0:.*]] = select i1 %[[ACT0]], i32 %[[V0]], i32 0
; CHECK: %[[DP0:.*]] = getelementptr inbounds i32, ptr %[[DSTA]], i32 0
; CHECK: store i32 %[[SEL0]], ptr %[[DP0]]
; CHECK: load <16 x i32>, ptr %[[DSTA]]

; NARROW-LABEL: define void @main(
; NARROW-NOT: alloca
; NARROW: %[[NIDX0:.*]] = extractelement <8 x i32> %rev.wide, i32 0
; NARROW: extractelement <8 x i1> %wave_entry_mask, i32 %[[NIDX0]]
; NARROW: extractelement <8 x i32> %tid1, i32 %[[NIDX0]]
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %lane = call i32 @llvm.dx.wave.getlaneindex()
  %rev = sub i32 15, %lane
  %val = call i32 @llvm.spv.wave.readlane.i32(i32 %tid, i32 %rev)
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.dx.wave.getlaneindex()
declare i32 @llvm.spv.wave.readlane.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="16,1,1" }
