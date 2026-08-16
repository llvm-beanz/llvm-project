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

; A `RWBuffer<float4>` (UAV TypedBuffer) accessed through
; `ResourceDescriptorHeap[%idx]`: this is the bindless counterpart of
; dxil-raise-resource-handles.ll's `typed_buffer_uav_vec4` case -- the CPU
; target's actual supported shape, since it accepts bindless shaders only
; (see feme/docs/FeMeCPUDesign.md's "Resource Model") -- confirming
; `raiseResourceHandleFromHeap` also reconstructs `ResourceProperties`'
; Word1 component-count field (bits 8-15) into a `<4 x float>` element
; type rather than a bare scalar `float`.
; CHECK-LABEL: define %dx.types.Handle @typed_buffer_heap_vec4(
define %dx.types.Handle @typed_buffer_heap_vec4(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.TypedBuffer", <4 x float>, 1, 0, 1) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 true)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.TypedBuffer", <4 x float>, 1, 0, 1) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 4106, i32 1033 })
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

; A texture (Texture1D, kind 1) whose `ResourceProperties` has no
; recoverable component type/count -- the same malformed-input case
; `dxil-raise-resource-handles.ll`'s `unhandled_texture` documents -- must
; be left as unmodified `dx.op.*` calls rather than erroring.
; CHECK-LABEL: define %dx.types.Handle @unhandled_texture(
define %dx.types.Handle @unhandled_texture(i32 %idx) {
  ; CHECK: call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218,
  ; CHECK: call %dx.types.Handle @dx.op.annotateHandle(i32 216,
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 1, i32 0 })
  ret %dx.types.Handle %h2
}

; A `Texture2D<float4>` (SRV) accessed through `ResourceDescriptorHeap[%idx]`
; -- the bindless counterpart of `dxil-raise-resource-handles.ll`'s
; `texture2d_srv`, and the CPU target's actual supported shape (bindless
; shaders only). `Texture2D` == kind 2; Word1 = 1033 is `ElementType::F32`
; (9) with `CompCount` 4.
; CHECK-LABEL: define %dx.types.Handle @texture2d_heap(
define %dx.types.Handle @texture2d_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 true)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  ret %dx.types.Handle %h2
}

; A `RWTexture2D<float4>` (UAV) accessed through `ResourceDescriptorHeap[%idx]`.
; CHECK-LABEL: define %dx.types.Handle @rwtexture2d_heap(
define %dx.types.Handle @rwtexture2d_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Texture", <4 x float>, 1, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 4098, i32 1033 })
  ret %dx.types.Handle %h2
}

; A `Texture2DMS<float4, 4>` (SRV, kind 3), exercising the `dx.MSTexture`
; path's extra sample-count field (Word1 bits 16-23): Word1 = 263177 is
; `ElementType::F32` (9), `CompCount` 4, `SampleCount` 4.
; CHECK-LABEL: define %dx.types.Handle @texture2dms_heap(
define %dx.types.Handle @texture2dms_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.MSTexture", <4 x float>, 0, 4, 1, 3) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 true)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 3, i32 263177 })
  ret %dx.types.Handle %h2
}

; A `FeedbackTexture2D` (kind 17): its whole `Word1` is a
; `SamplerFeedbackType` (`MinMip` == 0), not a packed component-type/count
; field, per Design.md's "Decision: texture and sampler handle kinds" field
; table.
; CHECK-LABEL: define %dx.types.Handle @feedbacktexture2d_heap(
define %dx.types.Handle @feedbacktexture2d_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.FeedbackTexture", 0, 17) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 true)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 true)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 17, i32 0 })
  ret %dx.types.Handle %h2
}

; A default (non-comparison) sampler (kind 14) accessed through
; `SamplerDescriptorHeap[%idx]`.
; CHECK-LABEL: define %dx.types.Handle @sampler_heap(
define %dx.types.Handle @sampler_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: call %dx.types.Handle @llvm.dx.resource.casthandle{{.*}}(target("dx.Sampler", 0) [[HANDLE]])
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 true, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 14, i32 0 })
  ret %dx.types.Handle %h2
}

; A comparison sampler (kind 14, `SamplerCmpOrHasCounter` bit set):
; Word0 = 14 | (1 << 15) = 32782.
; CHECK-LABEL: define %dx.types.Handle @comparison_sampler_heap(
define %dx.types.Handle @comparison_sampler_heap(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.Sampler", 1) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  %h1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 true, i1 false)
  %h2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %h1, %dx.types.ResourceProperties { i32 32782, i32 0 })
  ret %dx.types.Handle %h2
}

declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
