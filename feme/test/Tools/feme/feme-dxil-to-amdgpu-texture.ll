; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o
; RUN: od -An -tx1 -N4 %t.o | FileCheck %s

; Retargets a `Texture2D`/`RWTexture2D` pair plus a `cbuffer` of `half`
; scalars all the way to a real ISA (AMDGPU) through the full `feme` CLI --
; the exact shape (bound, non-bindless texture reads/writes alongside a
; 16-bit-typed cbuffer) a real `dxc -T cs_6_2 -enable-16bit-types` compute
; shader like:
;
;   Texture2D<half4> InputTexture : register(t0);
;   RWTexture2D<half4> OutputTexture : register(u0);
;   cbuffer FilterParameters : register(b0) { half Scale; };
;   [numthreads(8, 8, 1)]
;   void main(uint3 id : SV_DispatchThreadID) {
;     half4 texel = InputTexture.Load(int3(id.xy, 0));
;     OutputTexture[id.xy] = half4(texel.r * Scale, texel.gba);
;   }
;
; produces. Before feme::dxil::OpRaisingPass::raiseTextureStore/the
; generalized raiseCBufferLoadLegacy and feme::amdgpu::ResourceLoweringPass's
; `dx.Texture`/`dx.CBuffer` support existed, this failed with a diagnostic
; (`feme-dxil-to-amdgpu-unsupported-resource.ll`'s predecessor covered the
; cbuffer half of this exact gap; texture reads/writes were never even
; reachable that far, since `raiseTextureStore` did not exist at all).

target triple = "dxil-unknown-shadermodel6.2-compute"

%__cblayout_CB = type { half, half }

define void @main() #0 {
  %tex = call target("dx.Texture", <4 x half>, 0, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %out = call target("dx.Texture", <4 x half>, 1, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %cb = call target("dx.CBuffer", %__cblayout_CB)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)

  %x = call i32 @llvm.dx.thread.id(i32 0)
  %y = call i32 @llvm.dx.thread.id(i32 1)
  %coord0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 %y, i32 1

  %row = call {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8(
      target("dx.CBuffer", %__cblayout_CB) %cb, i32 0)
  %scale = extractvalue {half,half,half,half,half,half,half,half} %row, 0

  %texel = call <4 x half> @llvm.dx.resource.load.level(
      target("dx.Texture", <4 x half>, 0, 0, 1, 2) %tex, <2 x i32> %coord, i32 0, <2 x i32> zeroinitializer)
  %texel0 = extractelement <4 x half> %texel, i32 0
  %scaled0 = fmul half %texel0, %scale
  %result = insertelement <4 x half> %texel, half %scaled0, i32 0

  call void @llvm.dx.resource.store.texture(
      target("dx.Texture", <4 x half>, 1, 0, 1, 2) %out, <2 x i32> %coord, <4 x half> %result)
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
