; REQUIRES: directx-registered-target, amdgpu-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer
; RUN: feme --target=amdgcn-amd-amdhsa %t.dxcontainer -o %t.o
; RUN: od -An -tx1 -N4 %t.o | FileCheck %s

; Retargets a `RWTexture2D::GetDimensions(width, height)` call (DXIL's
; `.xy` `GetDimensions` field pair) all the way to a real ISA (AMDGPU)
; through the full `feme` CLI -- the exact construct a real
; `dxc -T cs_6_2 -enable-16bit-types` compute shader like:
;
;   RWTexture2D<half4> OutputTexture : register(u0);
;   [numthreads(8, 8, 1)]
;   void main(uint3 id : SV_DispatchThreadID) {
;     uint width, height;
;     OutputTexture.GetDimensions(width, height);
;     if (id.x < width && id.y < height)
;       OutputTexture[id.xy] = 0.0h;
;   }
;
; produces. Before `DXILOpLowering::lowerGetDimensionsXY` (the forward
; direction), `feme::dxil::OpRaisingPass::raiseGetDimensions`'s `.xy` case,
; and `feme::amdgpu::ResourceLoweringPass`'s `Binding::NumDimensionArgs`
; support all existed, this failed with a "resource handle type
; 'dx.Texture' is not supported" diagnostic: `GetDimensions`' `.y` field was
; left as an unraised extract, so `hasOnlySupportedUses` rejected the whole
; binding and left its handle (and the raw `dx.op.getDimensions` call) in
; place for `feme::verifyNoRaisedIRRemains` to catch.

target triple = "dxil-unknown-shadermodel6.2-compute"

define void @main() #0 {
  %out = call target("dx.Texture", <4 x half>, 1, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)

  %dims = call <2 x i32> @llvm.dx.resource.getdimensions.xy(
      target("dx.Texture", <4 x half>, 1, 0, 1, 2) %out)
  %width = extractelement <2 x i32> %dims, i32 0
  %height = extractelement <2 x i32> %dims, i32 1

  %x = call i32 @llvm.dx.thread.id(i32 0)
  %y = call i32 @llvm.dx.thread.id(i32 1)
  %inboundsx = icmp ult i32 %x, %width
  %inboundsy = icmp ult i32 %y, %height
  %inbounds = and i1 %inboundsx, %inboundsy
  br i1 %inbounds, label %store, label %exit

store:
  %coord0 = insertelement <2 x i32> poison, i32 %x, i32 0
  %coord = insertelement <2 x i32> %coord0, i32 %y, i32 1
  call void @llvm.dx.resource.store.texture(
      target("dx.Texture", <4 x half>, 1, 0, 1, 2) %out, <2 x i32> %coord, <4 x half> zeroinitializer)
  br label %exit

exit:
  ret void
}

attributes #0 = { "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
