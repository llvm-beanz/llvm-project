; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A shader using a raised, format-agnostic operation (`llvm.dx.thread.id`)
; and real control flow (a loop): before `feme::Driver::run`'s CPU-target
; retargeting path ran `feme::cpu::runPipeline` (the same
; Prepare/ResourceLowering/Linearize/SIMDize/WaveLowering/EntryWrapper
; pipeline `feme::cpu::JITEngine::create` runs before JIT-dispatching a
; shader), a raised module reached the host `TargetMachine` completely
; unlowered -- neither its `llvm.dx.*` intrinsics nor its structured-but-
; still-SPMD control flow are anything a generic `TargetMachine`
; understands, so instruction selection crashed instead of failing
; cleanly. This is now retargeted to a real object file exporting the
; `feme_cpu_entry_main` ABI symbol (see "Kernel ABI" in
; feme/docs/FeMeCPUDesign.md).
; RUN: feme --target=%feme_host_triple %t.dxcontainer -o %t.o
; RUN: llvm-nm %t.o | FileCheck %s

; CHECK: T feme_cpu_entry_main

target triple = "dxil-unknown-shadermodel6.6-compute"

define void @main() #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %loop]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %inc = add i32 %i, %tid
  %cond = icmp slt i32 %inc, 64
  br i1 %cond, label %loop, label %exit
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
