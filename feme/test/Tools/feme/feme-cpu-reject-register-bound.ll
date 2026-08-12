; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A register-bound resource handle (a shader binding a resource with
; ordinary register/space binding rather than through the descriptor heap)
; is rejected with a diagnostic when targeting the FeMe CPU target (see
; feme::cpu::checkSupportedRaisedOps and the "Resource Model" section of
; feme/docs/FeMeCPUDesign.md: the CPU target accepts bindless shaders only).
; RUN: not feme --target=%feme_host_triple %t.dxcontainer -o %t.o 2>&1 | FileCheck %s --check-prefix=REGISTER-BOUND
; REGISTER-BOUND: unsupported raised operation: 'llvm.dx.resource.handlefrombinding{{.*}}' is a register-bound resource handle

; The same module re-targeted back to DXIL is unaffected: the diagnostic is
; specific to the CPU target, not the raised operation itself.
; RUN: feme --target=dxil %t.dxcontainer -o %t.dxil.o
; RUN: od -An -c -N4 %t.dxil.o | FileCheck %s --check-prefix=DXBC-MAGIC
; DXBC-MAGIC: D   X   B   C

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
