; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap milestone 9: a barrier with no group-sync requirement
; (`DeviceMemoryBarrier`, raised as `llvm.dx.device.memory.barrier`) is
; memory-ordering-only -- it does not need every invocation in the group to
; converge, so `feme::cpu::EntryWrapperPass` replaces it with an in-place
; `fence` rather than splitting the wave body into regions (see "Barriers"
; in "Phase 6: Group Execution and Barriers" in
; feme/docs/FeMeCPUDesign.md). `Device`/`All` memory scope needs a fence
; visible across host threads (a different group's wave loop may run on a
; different one), unlike a `Group`-only barrier -- see
; entry-wrapper-barrier-region-split.ll's `syncscope("singlethread")` fence
; for that case.

; CHECK-LABEL: define internal void @main(
; CHECK-NOT: call void @llvm.dx.device.memory.barrier
; CHECK: fence acq_rel

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header:
; CHECK-NOT: wave.loop.header.0
; CHECK: call void @main(
; CHECK: wave.loop.exit:
; CHECK-NEXT: ret void
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  call void @llvm.dx.device.memory.barrier()
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @llvm.dx.device.memory.barrier()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
