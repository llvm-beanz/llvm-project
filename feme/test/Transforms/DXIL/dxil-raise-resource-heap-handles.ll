; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's bindless descriptor-heap handle raising
; (raiseResourceHandleFromHeap in OpRaising.cpp): a `dx.op.annotateHandle`
; (216) call whose handle operand is a `dx.op.createHandleFromHeap` (218)
; call is rewritten into a single `llvm.dx.resource.handlefromheap`
; intrinsic call returning the resource's `target("dx.")` handle type,
; reconstructed from `AnnotateHandle`'s constant
; `%dx.types.ResourceProperties` operand exactly as
; dxil-raise-resource-handles.ll's `CreateHandleFromBinding` case does --
; see that file and raiseResourceHandleFromHeap's comment for scope. This is
; a required raised operation for the FeMe CPU target, which accepts
; bindless shaders only (see feme/docs/FeMeCPUDesign.md's "Resource Model").

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }

; A `Buffer<float>` (SRV TypedBuffer) accessed through
; `ResourceDescriptorHeap[%idx]`, non-uniformly indexed.
; CHECK-LABEL: define %dx.types.Handle @typed_buffer_heap(
define %dx.types.Handle @typed_buffer_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 true)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 10, i32 265 })
  ret %dx.types.Handle %h2
}

; A `RWByteAddressBuffer` (UAV, unstructured RawBuffer), uniformly indexed.
; CHECK-LABEL: define %dx.types.Handle @raw_buffer_heap(
define %dx.types.Handle @raw_buffer_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 4107, i32 0 })
  ret %dx.types.Handle %h2
}

; A resource kind this pass doesn't (yet) reconstruct (Texture2D, kind 1)
; must be left as unmodified `dx.op.*` calls rather than erroring -- the
; same scope limit as `raiseResourceHandleFromBinding`, and how a sampler
; heap access is left alone too (see the function's comment).
; CHECK-LABEL: define %dx.types.Handle @unhandled_texture(
define %dx.types.Handle @unhandled_texture(i32 %idx) {
  ; CHECK: call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218,
  ; CHECK: call %dx.types.Handle @dx.op.annotateHandle(i32 216,
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 1, i32 0 })
  ret %dx.types.Handle %h2
}

declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
