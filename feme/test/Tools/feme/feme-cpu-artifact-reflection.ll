; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; roadmap R22: `feme::Driver`'s CPU-target retargeting path embeds the same
; versioned reflection artifact a JIT host reads back through
; `feme::cpu::CompiledStage::getArtifactInfo` (see ResourceInfo.h's file
; comment) as a read-only data symbol in the resulting object file, so an
; AOT host that never had the IR in hand still learns the resolved wave
; size, thread-group dimensions, and groupshared requirements.
; RUN: feme --target=%feme_host_triple --wave-size=8 %t.dxcontainer -o %t.o
; RUN: llvm-nm %t.o | FileCheck %s

; CHECK: {{[a-zA-Z]}} {{_?}}feme_cpu_info_main

target triple = "dxil-unknown-shadermodel6.6-compute"

@tile = addrspace(3) global [4 x i32] zeroinitializer, align 16

define void @main() #0 {
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
