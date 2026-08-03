; REQUIRES: directx-registered-target, spirv-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --from=dxil --to=spirv %t.dxcontainer -o %t.spv
; RUN: od -An -tx1 -N4 %t.spv | FileCheck %s

; Cross-translates a DXContainer into a SPIR-V module through the full `feme`
; CLI: import (feme::DXILImporter) -> raise `dx.op.*` calls
; (feme::dxil::OpRaisingPass) -> raise `dx.*` module metadata
; (feme::dxil::MetadataRaisingPass) -> expand the HLSL-specific math
; intrinsics (feme::dxil::IntrinsicExpansionPass) -> re-express the result in
; SPIR-V's own intrinsics and handle types (feme::spirv::RaisedLoweringPass)
; -> feme::TargetMachineBackend.
;
; The fixture is built by `llc` from this file's textual IR rather than
; checked in as a binary, per "Avoiding binary test fixtures" in
; feme/docs/Design.md. The shader model triple the DXIL carries names the
; pipeline stage, which the Driver preserves as the environment of the Vulkan
; triple it retargets to.

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

attributes #0 = { "hlsl.numthreads"="8,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; The SPIR-V magic number, little-endian.
; CHECK: 03 02 23 07
