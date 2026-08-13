; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; An unbounded traditional binding (a shader binding an unbounded resource
; array with ordinary register/space binding rather than through the
; descriptor heap) cannot be assigned a finite reserved heap prefix, so it
; is rejected with a diagnostic when targeting the FeMe CPU target (see
; feme::cpu::checkSupportedRaisedOps, feme::cpu::BoundResourceNormalizationPass,
; and "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md). A
; *finite* traditional binding is accepted instead -- see
; feme-cpu-accept-bound-resource.ll.
; RUN: not feme --target=%feme_host_triple %t.dxcontainer -o %t.o 2>&1 | FileCheck %s --check-prefix=REGISTER-BOUND
; REGISTER-BOUND: unsupported raised operation: 'llvm.dx.resource.handlefrombinding{{.*}}' is a register-bound resource handle

; The same module re-targeted back to DXIL is unaffected: the diagnostic is
; specific to the CPU target, not the raised operation itself.
; RUN: feme --target=dxil %t.dxcontainer -o %t.dxil.o
; RUN: od -An -c -N4 %t.dxil.o | FileCheck %s --check-prefix=DXBC-MAGIC
; DXBC-MAGIC: D   X   B   C

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main() #0 {
  ; A range size of 0 is DXIL's own spelling of an unbounded array (see
  ; DXILOpLowering::lowerToBindAndAnnotateHandle's "Binding.Size == 0"
  ; check, which `feme::dxil::OpRaisingPass` inverts).
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 0, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %id,
      <4 x float> <float 1.0, float 0.0, float 0.0, float 1.0>)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
