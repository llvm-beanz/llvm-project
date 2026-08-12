; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources -S %s | FileCheck %s

; Covers feme::cpu::ResourceLoweringPass's canonicalization of a bindless
; typed-buffer access (see "Resource Model" -> "Lowering" in
; feme/docs/FeMeCPUDesign.md): `llvm.dx.resource.handlefromheap` plus its
; load/store-typedbuffer accesses become `feme.cpu.resource.*` calls, and
; the rewritten function gains the six trailing resource/root-constant ABI
; parameters every rewritten function gets ("The heap operands come from
; new function parameters").

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @main(
; CHECK-SAME: i32 %idx, ptr %resource_heap, i32 %resource_heap_count,
; CHECK-SAME: ptr %sampler_heap, i32 %sampler_heap_count,
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
define void @main(i32 %idx) {
  ; CHECK: [[ELEMIDX:%.*]] = zext i32 %idx to i64
  ; CHECK: [[LOADED:%.*]] = call <4 x float> @feme.cpu.resource.load.typed.v4f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 3, i64 [[ELEMIDX]], i1 true)
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32 3, i1 false)
  %loaded = call {<4 x float>, i1}
      @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
          target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx)
  %val = extractvalue {<4 x float>, i1} %loaded, 0
  ; CHECK-NOT: extractvalue
  ; CHECK: call void @feme.cpu.resource.store.typed.v4f32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 3, i64 {{%.*}}, <4 x float> [[LOADED]], i1 true)
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %idx, <4 x float> %val)
  ret void
}

; The raised handle/access declarations are gone: nothing selects a call to
; them once every caller is rewritten.
; CHECK-NOT: @llvm.dx.resource.handlefromheap
; CHECK-NOT: @llvm.dx.resource.load.typedbuffer
; CHECK-NOT: @llvm.dx.resource.store.typedbuffer

; A statically-known heap index (3, above) is recorded in the heap-usage
; metadata "Heap usage discovery" describes: entry name, root constant size
; (0 -- not yet implemented), sampler heap use (false), then the sorted
; heap indices.
; CHECK: !feme.cpu.resources = !{![[MD:[0-9]+]]}
; CHECK: ![[MD]] = !{!"main", i32 0, i1 false, i32 3}

declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
    @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32, i1)
declare {<4 x float>, i1}
    @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
declare void
    @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)
