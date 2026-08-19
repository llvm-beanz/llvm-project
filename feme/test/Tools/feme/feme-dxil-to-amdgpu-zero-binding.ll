; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o
; RUN: od -An -tx1 -N4 %t.o | FileCheck %s

; The same `Texture2D`/`RWTexture2D` pair as feme-dxil-to-amdgpu-texture.ll,
; but targeting shader model 6.6 rather than 6.2: SM 6.6+ is the version
; boundary at which LLVM's own DirectX backend starts lowering resource
; handles via `CreateHandleFromBinding`/`AnnotateHandle` (opcodes 217/216)
; instead of the legacy `CreateHandle` (opcode 57) feme-dxil-to-amdgpu-
; texture.ll's SM 6.2 exercises -- see `raiseResourceHandleFromBinding` vs.
; `raiseLegacyCreateHandle` in OpRaising.cpp. Since both textures are bound
; at register/space 0 (`t0`/`u0`), `CreateHandleFromBinding`'s
; `%dx.types.ResBind { LowerBound=0, UpperBound=0, Space=0, Class=0 }`
; operand for the SRV is *entirely* zero-valued, which LLVM's constant
; folder represents as a `ConstantAggregateZero`, not a `ConstantStruct` --
; a distinct `Constant` subclass `raiseResourceHandleFromBinding` (via the
; shared `getConstStruct` helper) must recognize alongside `ConstantStruct`,
; since a register/space-0 binding (`t0`/`u0`/`b0`/`s0`) is arguably the
; single most common HLSL resource binding of all. Before that helper
; existed, this exact shape silently failed to raise, leaving its
; `dx.op.textureLoad.f32` calls unraised and surfacing as
; `feme::verifyNoRaisedIRRemains`'s "is not supported when targeting"
; diagnostic instead of a real object file. See dxil-raise-resource-
; handles.ll's `texture2d_srv_zero_binding` for the pass-level (rather than
; end-to-end) regression coverage of the same gap.

target triple = "dxil-unknown-shadermodel6.6-compute"

define void @main() #0 {
  %tex = call target("dx.Texture", <4 x float>, 0, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %out = call target("dx.Texture", <4 x float>, 1, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)

  %x = call i32 @llvm.dx.thread.id(i32 0)
  %y = call i32 @llvm.dx.thread.id(i32 1)
  %coord0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 %y, i32 1

  %texel = call <4 x float> @llvm.dx.resource.load.level(
      target("dx.Texture", <4 x float>, 0, 0, 1, 2) %tex, <2 x i32> %coord, i32 0, <2 x i32> zeroinitializer)

  call void @llvm.dx.resource.store.texture(
      target("dx.Texture", <4 x float>, 1, 0, 1, 2) %out, <2 x i32> %coord, <4 x float> %texel)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
