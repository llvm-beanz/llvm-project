; RUN: feme-opt --llvm -passes=feme-spirv-lower-raised -S %s | FileCheck %s

; feme::spirv::RaisedLoweringPass rewrites the raised, format-agnostic
; `llvm.dx.*` intrinsics feme::dxil::OpRaisingPass produces into the
; `llvm.spv.*` intrinsics and `target("spirv.*")` handle types LLVM's in-tree
; SPIRV backend consumes. See the "Raised LLVM IR -> SPIR-V" section of
; feme/docs/Design.md.

target triple = "spirv-unknown-vulkan-compute"

; Thread index queries are a straight substitution: both backends' intrinsic
; families are parallel lowerings of the same HLSL builtins.
; CHECK-LABEL: define i32 @thread_id(
define i32 @thread_id() {
  ; CHECK: call i32 @llvm.spv.thread.id.i32(i32 0)
  %1 = call i32 @llvm.dx.thread.id(i32 0)
  ret i32 %1
}

; A `RWBuffer<float4>` becomes a read-write (Sampled=2) buffer-dimension
; (Dim=5) image of scalar `float` with the four components folded into the
; Rgba32f (format 1) image format, and its accesses become pointer-based
; loads/stores, which is what the SPIRV backend selects OpImageRead/
; OpImageWrite from.
; CHECK-LABEL: define void @typed_buffer(
define void @typed_buffer(i32 %idx) {
  ; CHECK: [[H:%.*]] = call target("spirv.Image", float, 5, 2, 0, 0, 2, 1) @llvm.spv.resource.handlefrombinding{{.*}}(i32 0, i32 3, i32 1, i32 0, ptr @resource_s0_b3.str)
  ; CHECK: [[LP:%.*]] = call ptr @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", float, 5, 2, 0, 0, 2, 1) [[H]], i32 %idx)
  ; CHECK: [[V:%.*]] = load <4 x float>, ptr [[LP]]
  ; CHECK: [[SP:%.*]] = call ptr @llvm.spv.resource.getpointer{{.*}}(target("spirv.Image", float, 5, 2, 0, 0, 2, 1) [[H]], i32 %idx)
  ; CHECK: store <4 x float> [[V]], ptr [[SP]]
  ; CHECK-NOT: llvm.dx.resource
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i32 3, i32 1, i32 0, ptr null)
  %loaded = call {<4 x float>, i1}
      @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx)
  %value = extractvalue {<4 x float>, i1} %loaded, 0
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx, <4 x float> %value)
  ret void
}

; SPIR-V defines no three-component storage image format, so a resource that
; would need one is left unlowered rather than silently widened.
; CHECK-LABEL: define void @three_component_buffer(
define void @three_component_buffer(i32 %idx) {
  ; CHECK: call target("dx.TypedBuffer", <3 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding
  %h = call target("dx.TypedBuffer", <3 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v3f32_1_0_0t(i32 0, i32 4, i32 1, i32 0, ptr null)
  ret void
}

declare i32 @llvm.dx.thread.id(i32)
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v4f32_1_0_0t(i32, i32, i32, i32, ptr)
declare target("dx.TypedBuffer", <3 x float>, 1, 0, 0) @llvm.dx.resource.handlefrombinding.tdx.TypedBuffer_v3f32_1_0_0t(i32, i32, i32, i32, ptr)
declare {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
declare void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)
