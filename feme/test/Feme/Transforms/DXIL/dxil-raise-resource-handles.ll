; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's resource-handle raising
; (raiseResourceHandleFromBinding in OpRaising.cpp): a `dx.op.annotateHandle`
; (216) call whose handle operand is a `dx.op.createHandleFromBinding` (217)
; call is rewritten into a single `llvm.dx.resource.handlefrombinding`
; intrinsic call returning the resource's `target("dx.")` handle type,
; reconstructed from the two ops' constant `%dx.types.ResBind`/
; `%dx.types.ResourceProperties` operands -- see that function's comment for
; the (intentionally narrow, TypedBuffer/unstructured-RawBuffer-only) scope.
; dxil-raise-resource-handles-roundtrip.ll separately validates this against
; real `-dxil-op-lower` output.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

%dx.types.Handle = type { ptr }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }

; A `Buffer<float>` (SRV TypedBuffer) bound at register t1, space 0: an
; unbounded array's `t1` slot is index 0 of the binding, so
; `CreateHandleFromBinding`'s index operand (1) is biased by `LowerBound`
; (1) and must be un-biased back to 0.
; CHECK-LABEL: define %dx.types.Handle @typed_buffer_srv(
define %dx.types.Handle @typed_buffer_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 10, i32 265 })
  ret %dx.types.Handle %h2
}

; A `RWByteAddressBuffer` (UAV, unstructured RawBuffer) bound at register u0.
; CHECK-LABEL: define %dx.types.Handle @raw_buffer_uav(
define %dx.types.Handle @raw_buffer_uav(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", i8, 1, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.RawBuffer", i8, 1, 0) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 1 }, i32 0, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 4107, i32 0 })
  ret %dx.types.Handle %h2
}

; An unbounded array of resources (`UpperBound` = ~0u) must be reconstructed
; as an unbounded binding (`Size` = 0), not `0xFFFFFFFF - LowerBound + 1`.
; CHECK-LABEL: define %dx.types.Handle @unbounded_array(
define %dx.types.Handle @unbounded_array(i32 %idx) {
  ; CHECK: call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 0, i32 %idx, ptr null)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 4294967295, i32 0, i8 0 }, i32 %idx, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 10, i32 265 })
  ret %dx.types.Handle %h2
}

; A resource kind this pass doesn't (yet) reconstruct (StructuredBuffer,
; kind 12) must be left as unmodified `dx.op.*` calls rather than erroring.
; CHECK-LABEL: define %dx.types.Handle @unhandled_structured_buffer(
define %dx.types.Handle @unhandled_structured_buffer(i32 %idx) {
  ; CHECK: call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217,
  ; CHECK: call %dx.types.Handle @dx.op.annotateHandle(i32 216,
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 0 }, i32 0, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 12, i32 16 })
  ret %dx.types.Handle %h2
}

declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
