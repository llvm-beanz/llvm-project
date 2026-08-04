; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --target=dxil %t.dxcontainer -o %t.out.dxcontainer
; RUN: od -An -c -N4 %t.out.dxcontainer | FileCheck %s

; Round-trips a DXContainer all the way back out to a DXContainer through the
; full `feme` CLI: import (feme::DXILImporter) -> raise `dx.op.*` calls
; (feme::dxil::OpRaisingPass) -> raise `dx.*` module metadata
; (feme::dxil::MetadataRaisingPass) -> feme::TargetMachineBackend targeting
; DXIL again.
;
; The fixture is built by `llc` from this file's textual IR rather than
; checked in as a binary, per "Avoiding binary test fixtures" in
; feme/docs/Design.md. It deliberately targets shader model 6.0, so that
; LLVM's own `DXILOpLowering` emits the *legacy* `dx.op.createHandle` op (57)
; -- the form `dxc` still emits by default, which names its resource
; indirectly through `!dx.resources` metadata -- exercising that raising path
; end to end. Nothing here reconstructs the entry point or shader model on the
; way back out except `MetadataRaisingPass`: without it the re-emitted
; container would have no entry point at all.

target triple = "dxil-unknown-shadermodel6.0-compute"

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

; CHECK: D   X   B   C
