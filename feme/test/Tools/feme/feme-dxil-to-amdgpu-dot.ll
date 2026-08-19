; REQUIRES: amdgpu-registered-target
; RUN: llvm-as %s -o %t.bc
; RUN: feme --target=amdgcn-amd-amdhsa %t.bc -o %t.o
; RUN: od -An -tx1 -N4 %t.o | FileCheck %s

; Retargets a DXIL module using SM6.9's unified `FDot` op (DXIL opcode 311,
; `dx.op.dot.*`) all the way to a real ISA (AMDGPU) through the full `feme`
; CLI. Unlike `feme-dxil-to-amdgpu.ll` and its siblings, this uses
; `llvm-as`'s raw-bitcode path (see test/Import/DXIL/dxil-import.ll) rather
; than `llc`-producing a real DXContainer: LLVM's own DirectX backend always
; scalarizes `llvm.dx.fdot` (`DXILIntrinsicExpansion.cpp`) before
; `DXILOpLowering` ever runs, so it can never actually emit a `dx.op.dot`
; call for `llc` to round-trip through -- only a real external frontend
; (e.g. `dxc -T cs_6_9`, which is where this repro came from) does. This
; hand-writes that call directly instead, matching the shape DXC's `dot()`
; codegen produces for a `half2`/`half3` operand (see e.g.
; `dxc -T cs_6_9 -Fc` on a shader calling `dot()` on 16-bit vectors).
;
; Before feme::dxil::OpRaisingPass's `{311, Intrinsic::dx_fdot, true}` row
; and feme::dxil::IntrinsicExpansionPass's `expandFDot` existed, this failed
; with "'dx.op.dot.v2f32' is not supported when targeting
; 'amdgcn-amd-amdhsa'".

target triple = "dxil-unknown-shadermodel6.9-compute"

define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %id = call i32 @llvm.dx.thread.id(i32 0)
  %dot = call float @dx.op.dot.v2f32(i32 311, <2 x float> <float 1.0, float 2.0>, <2 x float> <float 3.0, float 4.0>)
  %v = insertelement <4 x float> zeroinitializer, float %dot, i32 0
  call void @llvm.dx.resource.store.typedbuffer(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %id, <4 x float> %v)
  ret void
}

declare float @dx.op.dot.v2f32(i32, <2 x float>, <2 x float>)
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare i32 @llvm.dx.thread.id(i32)
declare void @llvm.dx.resource.store.typedbuffer(target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)

attributes #0 = { "hlsl.numthreads"="1024,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}

; CHECK: 7f 45 4c 46
