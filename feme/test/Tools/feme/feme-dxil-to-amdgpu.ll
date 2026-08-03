; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --from=dxil --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o
; RUN: od -An -tx1 -N4 %t.o | FileCheck %s

; Retargets a DXIL module all the way to a real ISA (AMDGPU) through the full
; `feme` CLI: import (feme::DXILImporter) -> raise dx.op.* calls back to
; idiomatic llvm.dx.* intrinsics (feme::dxil::OpRaisingPass) -> lower those to
; AMDGPU's own intrinsics (feme::amdgpu::RaisedLoweringPass) ->
; feme::TargetMachineBackend targeting "amdgcn-amd-amdhsa". `llc` (targeting
; a `dxil-...` triple) builds this file's textual IR into a real DXContainer
; with an embedded DXIL bitcode part first, the same way
; test/Import/DXIL/dxil-import-container.ll does, per "Avoiding binary test
; fixtures" in feme/docs/Design.md.
;
; This is the shape of a real compute shader: it writes its dispatch-wide
; result to a `RWBuffer<float4>` (so the DXIL resource ops have to be raised
; and then re-expressed as an AMDGPU kernel pointer argument), indexes that
; write by `SV_DispatchThreadID` (which has no single AMDGPU intrinsic), and
; uses an HLSL-specific math op with no AMDGPU equivalent (`frac`).

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  %f = call float @llvm.dx.frac.f32(float 1.500000e+00)
  %v = insertelement <4 x float> zeroinitializer, float %f, i32 0
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %id, <4 x float> %v)
  ret void
}

attributes #0 = { "hlsl.numthreads"="1024,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
