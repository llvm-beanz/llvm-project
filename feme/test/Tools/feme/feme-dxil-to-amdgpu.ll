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
; Deliberately uses only the one opcode family feme::amdgpu::RaisedLoweringPass
; currently covers (thread/group index queries, see the "Raised LLVM IR ->
; AMDGPU" section of feme/docs/Design.md) plus a directly-mapped math op
; (both fully covered by feme::dxil::OpRaisingPass): real shaders' resource
; load/store calls are not yet raised (see the DXIL section of
; feme/docs/Design.md), so are out of scope for this plumbing-validation
; test, matching the SPIR-V "null pipeline"'s own precedent of validating
; Driver/Translator/Backend wiring without depending on coverage that
; doesn't exist yet.

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main() #0 {
  %1 = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %2 = call float @llvm.sin.f32(float 1.000000e+00)
  ret void
}

declare i32 @llvm.dx.thread.id.in.group(i32)
declare float @llvm.sin.f32(float)

attributes #0 = { "hlsl.numthreads"="1024,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
