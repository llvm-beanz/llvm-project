; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A *finite* traditional binding (`register(u0, space0)`, a single-element
; range) is accepted when targeting the FeMe CPU target:
; feme::cpu::BoundResourceNormalizationPass normalizes it into a heap access
; before feme::cpu::checkSupportedRaisedOps ever sees it (see
; "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md) -- contrast
; with feme-cpu-reject-unbounded-register-bound.ll's unbounded range, which
; is still rejected.
; RUN: feme --target=%feme_host_triple %t.dxcontainer -o %t.o
; RUN: llvm-nm %t.o | FileCheck %s

; CHECK: T {{_?}}feme_cpu_entry_main

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %id,
      <4 x float> <float 1.0, float 0.0, float 0.0, float 1.0>)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
