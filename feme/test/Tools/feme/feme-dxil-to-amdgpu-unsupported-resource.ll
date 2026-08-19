; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: not feme --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o 2>&1 | FileCheck %s

; A `StructuredBuffer`/`ByteAddressBuffer`'s `target("dx.RawBuffer", ...)`
; handle is a resource kind `feme::amdgpu::ResourceLoweringPass` does not
; model (only a typed buffer's `target("dx.TypedBuffer", ...)`, a texture's
; `target("dx.Texture", ...)`, a cbuffer's `target("dx.CBuffer", ...)`, or
; SPIR-V's `target("spirv.Image", ...)` are -- see its own class comment);
; it is therefore left entirely unrewritten, per that pass's documented
; "leave what it cannot model alone" precedent (feme/docs/Design.md's
; "Raised LLVM IR -> AMDGPU" section) -- here surfacing as
; `feme::verifyNoRaisedIRRemains`'s "used in function" diagnostic for the
; still-live `llvm.dx.resource.handlefrombinding` call it leaves behind
; (rather than its "resource handle type ... produced in function" one,
; which fires only once a handle has itself been raised into an
; instruction feme::dxil::OpRaisingPass produced -- an already-canonical
; `llvm.dx.resource.*` input like this one, with no `dx.op.*` calling
; convention to raise from, never reaches that path). Before
; `feme::verifyNoRaisedIRRemains` existed, a leftover `target("dx.")`
; handle type like this one reached AMDGPU's real instruction selection
; unchecked and hit `llvm::MVT::getVT`'s
; `llvm_unreachable("Unknown target ext type!")`; it is now a clean
; diagnostic instead.
;
; This test previously used a `cbuffer` (`target("dx.CBuffer", ...)`) here,
; since that resource kind used to be one `ResourceLoweringPass` did not
; model either -- a real HLSL compute shader compiled with
; `dxc -T cs_6_8 -enable-16bit-types` (a `Texture2D`/`RWTexture2D` pair plus
; a `cbuffer` of `half` scalars) hit exactly that gap. `dx.CBuffer` (and
; `dx.Texture`) are now modeled (see amdgpu-lower-resources.ll's
; `cbuffer_row`/texture cases and feme-dxil-to-amdgpu-texture.ll's real
; end-to-end shader), so this test was repointed at a resource kind that is
; still genuinely unsupported instead.

target triple = "dxil-unknown-shadermodel6.5-compute"

%struct.S = type { float }

define void @main() #0 {
  %sb = call target("dx.RawBuffer", %struct.S, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %v = call {%struct.S, i1} @llvm.dx.resource.load.rawbuffer(
      target("dx.RawBuffer", %struct.S, 0, 0) %sb, i32 0, i32 0)
  %s = extractvalue {%struct.S, i1} %v, 0
  %f = extractvalue %struct.S %s, 0
  %out = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  %vv = insertelement <4 x float> zeroinitializer, float %f, i32 0
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %out, i32 %id, <4 x float> %vv)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: {{.*}}llvm.dx.resource.handlefrombinding{{.*}}' is not supported when targeting 'amdgcn-amd-amdhsa' (used in function 'main')
