; RUN: feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-vertex -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A raised barrier intrinsic in a vertex shader (e.g. from SPIR-V
; `OpMemoryBarrier`/`OpControlBarrier`) is lowered to a fence, as in the
; fragment stage: device scope needs a system fence, group scope stays on
; one host thread.

; CHECK-LABEL: define internal void @vs_main(
; CHECK-NOT: llvm.spv
; CHECK: fence acq_rel
; CHECK: fence syncscope("singlethread") acq_rel

define void @vs_main() #0 !feme.signature !0 {
  %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  call void @llvm.spv.device.memory.barrier()
  call void @llvm.spv.group.memory.barrier()
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %in, i32 0)
  ret void
}

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
declare void @llvm.spv.device.memory.barrier()
declare void @llvm.spv.group.memory.barrier()

attributes #0 = { "feme.shader.stage"="vertex" "feme.cpu.wavesize"="4" }

!0 = !{[224 x i8] c"\09\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}
