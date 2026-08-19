; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: not feme --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o 2>&1 | FileCheck %s

; A `cbuffer`'s `target("dx.CBuffer", ...)` handle is a resource kind
; `feme::amdgpu::ResourceLoweringPass` does not model (only a typed buffer's
; `target("dx.TypedBuffer", ...)`/SPIR-V's `target("spirv.Image", ...)` are
; -- see its own class comment); it is therefore left entirely unrewritten,
; per that pass's documented "leave what it cannot model alone" precedent
; (feme/docs/Design.md's "Raised LLVM IR -> AMDGPU" section). Before
; `feme::verifyNoRaisedIRRemains` existed, that leftover `target("dx.")`
; handle type reached AMDGPU's real instruction selection unchecked and hit
; `llvm::MVT::getVT`'s `llvm_unreachable("Unknown target ext type!")` --
; this is the exact shape (a `cbuffer` of scalars alongside a bound
; `Texture2D`/`RWTexture2D` pair) a real HLSL compute shader compiled with
; `dxc -T cs_6_8 -enable-16bit-types` hit that crash with. It is now a clean
; diagnostic instead.

target triple = "dxil-unknown-shadermodel6.5-compute"

%__cblayout_CB = type { float }

define void @main() #0 {
  %cb = call target("dx.CBuffer", %__cblayout_CB)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %row = call {float, float, float, float} @llvm.dx.resource.load.cbufferrow.4(
      target("dx.CBuffer", %__cblayout_CB) %cb, i32 0)
  %v = extractvalue {float, float, float, float} %row, 0
  %out = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  %vv = insertelement <4 x float> zeroinitializer, float %v, i32 0
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %out, i32 %id, <4 x float> %vv)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: resource handle type 'dx.CBuffer' is not supported when targeting 'amdgcn-amd-amdhsa'
