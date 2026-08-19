; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's resource-handle raising
; (raiseResourceHandleFromBinding in OpRaising.cpp): a `dx.op.annotateHandle`
; (216) call whose handle operand is a `dx.op.createHandleFromBinding` (217)
; call is rewritten into a single `llvm.dx.resource.handlefrombinding`
; intrinsic call returning the resource's `target("dx.")` handle type,
; reconstructed from the two ops' constant `%dx.types.ResBind`/
; `%dx.types.ResourceProperties` operands -- see that function's comment for
; scope (`TypedBuffer`/`RawBuffer` element types are recovered exactly;
; `StructuredBuffer`/`CBuffer` only recover size/alignment, so those get an
; opaque size-only placeholder element type via `getOpaqueSizedType`).
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

; A `RWBuffer<float4>` (UAV TypedBuffer) bound at register u1, space 0:
; `ResourceProperties`' Word1 carries not just the scalar component type
; (`F32`, bits 0-7) but also the element's component count (`4`, bits
; 8-15 -- see `ResourceInfo::getAnnotateProps`), so the reconstructed
; handle's element type must be `<4 x float>`, not the bare `float` that
; only decoding the component type would produce.
; CHECK-LABEL: define %dx.types.Handle @typed_buffer_uav_vec4(
define %dx.types.Handle @typed_buffer_uav_vec4(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", <4 x float>, 1, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.TypedBuffer", <4 x float>, 1, 0, 1) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 1 }, i32 1, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 4106, i32 1033 })
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

; A texture (Texture1D, kind 1) whose `ResourceProperties` has no recoverable
; component type/count (Word1 = 0, i.e. `ElementType::Invalid`, `CompCount`
; 0 -- malformed input, not a real DXIL module) must be left as unmodified
; `dx.op.*` calls rather than erroring: see `buildAnnotatedHandleType`'s
; `widenToTypedBufferElement`/`getElementLLVMType` calls, shared with
; `TypedBuffer`.
; CHECK-LABEL: define %dx.types.Handle @unhandled_texture(
define %dx.types.Handle @unhandled_texture(i32 %idx) {
  ; CHECK: call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217,
  ; CHECK: call %dx.types.Handle @dx.op.annotateHandle(i32 216,
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 0 }, i32 0, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 1, i32 0 })
  ret %dx.types.Handle %h2
}

; A `Texture2D<float4>` (SRV, kind 2) bound at register t3, exercising
; `buildAnnotatedHandleType`'s texture path (see Design.md's "Decision:
; texture and sampler handle kinds"): raises to `dx.Texture` with the
; dimension (`ResourceKind::Texture2D` == 2) as the trailing int parameter,
; sharing `TypedBuffer`'s component type/count decode (Word1 = 1033 is
; `ElementType::F32` (9) with `CompCount` 4, exactly as
; `typed_buffer_uav_vec4` above).
; CHECK-LABEL: define %dx.types.Handle @texture2d_srv(
define %dx.types.Handle @texture2d_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 3, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  ret %dx.types.Handle %h2
}

; A `Texture2D<float4>` (SRV, kind 2) bound at register t0, space 0: every
; field of its `%dx.types.ResBind` (`LowerBound`/`UpperBound`/`Space`, all 0)
; is zero, so LLVM's constant folder represents the whole struct literal as
; a `ConstantAggregateZero` rather than a `ConstantStruct` -- a distinct
; `Constant` subclass `getConstStruct` (used by
; `raiseResourceHandleFromBinding`/`raiseResourceHandleFromHeap`) must
; recognize alongside `ConstantStruct`, since `dyn_cast<ConstantStruct>`
; alone rejects it and leaves every resource bound at register/space 0
; (i.e. `t0`/`u0`/`b0`/`s0`, the most common binding of all) unraised.
; CHECK-LABEL: define %dx.types.Handle @texture2d_srv_zero_binding(
define %dx.types.Handle @texture2d_srv_zero_binding(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  ret %dx.types.Handle %h2
}

; A comparison sampler bound at register s0, exercising `dx.Sampler`'s
; single-bit `SamplerCmpOrHasCounter` decode (Word0 bit 15): `Sampler` ==
; kind 14, with bit 15 set, i.e. Word0 = 14 | (1 << 15) = 32782.
; CHECK-LABEL: define %dx.types.Handle @comparison_sampler(
define %dx.types.Handle @comparison_sampler(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Sampler", 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Sampler", 1) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 32782, i32 0 })
  ret %dx.types.Handle %h2
}

; A `StructuredBuffer<S>` (SRV) bound at register t2, where `S` is a struct
; whose largest member is a `<4 x i32>` (align 16), so its 20-byte payload
; rounds up to a 32-byte, align-16 stride -- the same values a real
; `-dxil-op-lower` run on `%struct.S = type { float, <4 x i32> }` produces
; (see dxil-raise-resource-handles-roundtrip.ll). `StructuredBuffer`'s
; original field layout isn't recoverable from binding metadata alone, so
; the element type is an opaque size/alignment-only placeholder
; (`getOpaqueSizedType`), not `%struct.S` itself.
; CHECK-LABEL: define %dx.types.Handle @structured_buffer_srv(
define %dx.types.Handle @structured_buffer_srv(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.RawBuffer", { <4 x i32>, [16 x i8] }, 0, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.RawBuffer", { <4 x i32>, [16 x i8] }, 0, 0) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 1036, i32 32 })
  ret %dx.types.Handle %h2
}

; A `StructuredBuffer` whose element size (12 bytes) isn't a multiple of its
; encoded alignment (align 8, i.e. `AlignLog2` = 3): this can't happen for a
; real struct's alloc size (alignment always evenly divides alloc size), so
; `getOpaqueSizedType` falls back to a plain byte array rather than
; constructing a self-contradictory type.
; CHECK-LABEL: define %dx.types.Handle @structured_buffer_misaligned(
define %dx.types.Handle @structured_buffer_misaligned(i32 %idx) {
  ; CHECK: call target("dx.RawBuffer", [12 x i8], 0, 0) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 2, i32 1, i32 0, ptr null)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 780, i32 12 })
  ret %dx.types.Handle %h2
}

; A `cbuffer` bound at register b2: its `ResourceProperties` encoding never
; carries alignment bits (`AlignLog2` is only ever set for
; `StructuredBuffer`), so its opaque placeholder element type is always a
; plain byte array.
; CHECK-LABEL: define %dx.types.Handle @cbuffer_case(
define %dx.types.Handle @cbuffer_case(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.CBuffer", [32 x i8]) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 2, i32 1, i32 0, ptr null)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.CBuffer", [32 x i8]) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 13, i32 32 })
  ret %dx.types.Handle %h2
}

declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
