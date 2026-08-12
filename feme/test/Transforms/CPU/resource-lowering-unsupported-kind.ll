; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s

; A resource kind feme::cpu::ResourceLoweringPass doesn't yet canonicalize
; (a constant buffer, here) leaves the whole function untouched rather than
; being partially rewritten -- see the "Scope" note in
; feme/include/feme/Transforms/CPU/ResourceLowering.h. A register-bound
; handle is similarly untouched (`feme::cpu::checkSupportedRaisedOps`
; rejects those earlier in the real pipeline; this pass doesn't need to
; reject them itself, only to leave them alone).

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @cbuffer_via_heap(
; CHECK-NOT: resource_heap
; CHECK: call target("dx.CBuffer", {{.*}}) @llvm.dx.resource.handlefromheap
define void @cbuffer_via_heap() {
  %h = call target("dx.CBuffer", [16 x i8])
      @llvm.dx.resource.handlefromheap.tdx.CBuffer_a16i8t(i32 0, i1 false)
  ret void
}

; CHECK-LABEL: define void @register_bound(
; CHECK-NOT: resource_heap
; CHECK: call target("dx.TypedBuffer", {{.*}}) @llvm.dx.resource.handlefrombinding
define void @register_bound() {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  ret void
}

declare target("dx.CBuffer", [16 x i8])
    @llvm.dx.resource.handlefromheap.tdx.CBuffer_a16i8t(i32, i1)
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
    @llvm.dx.resource.handlefrombinding(i32, i32, i32, i32, ptr)
